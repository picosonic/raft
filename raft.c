#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>

#include <libssh/libssh.h>
#include <libssh/sftp.h>

#include "b64.h"

#define BLOCKSIZE (10*1024)

int verbose=0;

int transferfile(ssh_session session, const char *localpath, const char *remotepath)
{
  sftp_session sftp;
  sftp_file file;
  int rc;
  struct stat localstat;
  FILE *fp;

  // Check local file exists and get file stats
  rc=stat(localpath, &localstat);
  if (rc!=0)
    return SSH_ERROR;

  // Check local file is a file
  if (!S_ISREG(localstat.st_mode))
    return SSH_ERROR;

  if (verbose==1)
    fprintf(stderr, "Local file '%s' exists\n", localpath);

  // Create SFTP session
  sftp=sftp_new(session);
  if (sftp==NULL)
    return SSH_ERROR;

  // Initialize the sftp session with the server
  rc=sftp_init(sftp);
  if (rc!=SSH_OK)
  {
    // Close and deallocate a sftp session
    sftp_free(sftp);
    return rc;
  }

  if (verbose==1)
    fprintf(stderr, "SFTP session created and initialised\n");

  // Open local file for reading
  fp=fopen(localpath, "rb");
  if (fp!=NULL)
  {
    struct timeval timestamps[2];
    size_t remaining=localstat.st_size;

    // Open remote file for writing
    file=sftp_open(sftp, remotepath, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
    if (file==NULL)
    {
      // Close the local file
      fclose(fp);

      // Close and deallocate a sftp session
      sftp_free(sftp);

      if (verbose==1)
        fprintf(stderr, "Failed to open remote file '%s' for writing\n", remotepath);

      return SSH_ERROR;
    }

    if (verbose==1)
      fprintf(stderr, "Starting file transfer\n");

    // Loop until whole of local file is read
    while (remaining>0)
    {
      unsigned char buffer[BLOCKSIZE];
      size_t numread;
      size_t numwritten;

      // Read local block
      numread=fread(buffer, 1, BLOCKSIZE, fp);

      // Write remote block
      numwritten=sftp_write(file, buffer, numread);

      if (numwritten<numread)
      {
        // Close remote file
        rc=sftp_close(file);
        if (rc!=SSH_NO_ERROR)
          fprintf(stderr, "Error closing remote file: %s\n", ssh_get_error(session));

        // Delete the broken remote file
        //rc=sftp_unlink(sftp, remotepath);

        // Close local file
        fclose(fp);

        // Close and deallocate a sftp session
        sftp_free(sftp);

        return SSH_ERROR;
      }

      remaining-=numread;
    }

    // Close remote file
    rc=sftp_close(file);
    if (rc!=SSH_NO_ERROR)
      fprintf(stderr, "Error closing remote file: %s\n", ssh_get_error(session));

    // Close local file
    fclose(fp);

    if (verbose==1)
      fprintf(stderr, "File transfer complete\n");

    // Change remote file mode (permissions) to match local file
    sftp_chmod(sftp, remotepath, localstat.st_mode);

    // Change remote file owner to match local file
    sftp_chown(sftp, remotepath, localstat.st_uid, localstat.st_gid);

    // Change remote file timestamps
    timestamps[0].tv_sec=localstat.st_atime;
    timestamps[0].tv_usec=0;
    timestamps[1].tv_sec=localstat.st_mtime;
    timestamps[1].tv_usec=0;
    sftp_utimes(sftp, remotepath, (struct timeval *) &timestamps);
  }

  // Close and deallocate a sftp session
  sftp_free(sftp);
  return SSH_OK;
}

int runcommand(ssh_session session, const char *cmdstr)
{
  ssh_channel channel;
  int rc;
  char buffer[256];

  channel=ssh_channel_new(session);
  if (channel==NULL)
    return SSH_ERROR;

  rc=ssh_channel_open_session(channel);
  if (rc!=SSH_OK)
  {
    ssh_channel_free(channel);
    return rc;
  }

  rc=ssh_channel_request_exec(channel, cmdstr);
  if (rc!=SSH_OK)
  {
    ssh_channel_close(channel);
    ssh_channel_free(channel);
    return rc;
  }

  while ((ssh_channel_is_open(channel)) && (!ssh_channel_is_eof(channel)))
  {
    int nbytes;

    // Blocking read
    nbytes=ssh_channel_read(channel, buffer, sizeof(buffer), 0);

    // Check for error
    if (nbytes<0)
    {
      ssh_channel_close(channel);
      ssh_channel_free(channel);
      return SSH_ERROR;
    }

    // Check for valid read
    if (nbytes>0)
    {
      int i;

      for (i=0; i<nbytes; i++)
        printf("%c", buffer[i]);
    }

    // 0 means EOF
    if (nbytes==0) break;
  }

  ssh_channel_send_eof(channel);
  ssh_channel_close(channel);
  ssh_channel_free(channel);

  return SSH_OK;
}

void showargs()
{
  fprintf(stderr, "raft - Remote Administration File Transfer\n\n");
  fprintf(stderr, "Syntax : [-h [username@]remotehost] [-i identityfile] [-p password] [-c \"shell command\"] [-l localfilename] [-r remotefilename] [-z] [-v]\n");
}

int main(int argc, char **argv)
{
  int argn=0;
  ssh_session session;
  int verbosity=SSH_LOG_NOLOG;
  int compression=0;
  char hostname[256]="127.0.0.1";
  char username[256]="root";
  char password[256]="";
  char commandstr[1024]="";
  char identityfile[1024]="";
  char localpath[1024]="";
  char remotepath[1024]="";
  int port=22;
  long timeout=20;
  int rc;
  struct passwd *user=getpwuid(getuid());

  // Check for no arguments
  if (argc==1)
  {
    showargs();
    return 1;
  }

  // Set username to current logged in user
  if (user!=NULL)
    strcpy(username, user->pw_name);

  // Set values from command line
  while (argn<argc)
  {
    if ((strcmp(argv[argn], "-h")==0) && ((argn+1)<argc))
    {
      char *atpos;
      ++argn;

      // Check for username override
      atpos=strstr(argv[argn], "@");
      if (atpos!=NULL)
      {
        atpos[0]=0;
        strcpy(username, argv[argn]);
        strcpy(hostname, &atpos[1]);
      }
      else
        strcpy(hostname, argv[argn]);
    }
    else
    if ((strcmp(argv[argn], "-i")==0) && ((argn+1)<argc))
    {
      ++argn;
      strcpy(identityfile, argv[argn]);
    }
    else
    if ((strcmp(argv[argn], "-p")==0) && ((argn+1)<argc))
    {
      ++argn;
      strcpy(password, argv[argn]);
    }
    else
    if ((strcmp(argv[argn], "-c")==0) && ((argn+1)<argc))
    {
      ++argn;
      strcpy(commandstr, argv[argn]);
    }
    else
    if ((strcmp(argv[argn], "-c64")==0) && ((argn+1)<argc))
    {
      char *decoded;

      ++argn;

      decoded=strdup(argv[argn]);
      if (decoded!=NULL)
      {
        b64_decode_string(argv[argn], decoded, strlen(argv[argn]));
        strcpy(commandstr, decoded);
        free(decoded);
      }
    }
    else
    if ((strcmp(argv[argn], "-l")==0) && ((argn+1)<argc))
    {
      ++argn;
      strcpy(localpath, argv[argn]);
    }
    else
    if ((strcmp(argv[argn], "-r")==0) && ((argn+1)<argc))
    {
      ++argn;
      strcpy(remotepath, argv[argn]);
    }
    else
    if (strcmp(argv[argn], "-z")==0)
    {
      compression=9;
    }
    else
    if (strcmp(argv[argn], "-v")==0)
    {
      verbose=1;
    }

    ++argn;
  }

  if (verbose==1)
    verbosity=SSH_LOG_PROTOCOL;

  // Create ssh session
  session=ssh_new();
  if (session==NULL)
    exit(1);

  // Set ssh options
  ssh_options_set(session, SSH_OPTIONS_HOST, hostname);
  ssh_options_set(session, SSH_OPTIONS_USER, username);
  ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
  ssh_options_set(session, SSH_OPTIONS_PORT, &port);
  ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);

  // Check for compression
  if (compression>0)
  {
    ssh_options_set(session, SSH_OPTIONS_COMPRESSION, "yes");
    ssh_options_set(session, SSH_OPTIONS_COMPRESSION_LEVEL, &compression);
  }

  // Connect to remote host
  rc=ssh_connect(session);
  if (rc!=SSH_OK)
  {
    fprintf(stderr, "Error connecting to '%s': %s\n", hostname, ssh_get_error(session));
    exit(2);
  }

  if (verbose==1)
    fprintf(stderr, "Connected\n");

  // Authenticate to remote host
  if (strlen(identityfile)>0)
  {
    // Authenticate using private key and passphrase
    rc=ssh_userauth_privatekey_file(session, username, identityfile, password);
    if (rc != SSH_OK)
    {
      fprintf(stderr, "Error importing key: %s\n", ssh_get_error(session));
      ssh_disconnect(session);
      ssh_free(session);
      exit(3);
    }
  }
  else
  {
    // Authenticate using username and password
    rc=ssh_userauth_password(session, NULL, password);
    if (rc!=SSH_AUTH_SUCCESS)
    {
      fprintf(stderr, "Error authenticating with password: %s\n", ssh_get_error(session));
      ssh_disconnect(session);
      ssh_free(session);
      exit(4);
    }
  }

  if (verbose==1)
    fprintf(stderr, "Authenticated\n");

  // Do local to remote file transfer
  if ((localpath[0]!=0) && (remotepath[0]!=0))
    rc=transferfile(session, localpath, remotepath);

  if (rc!=SSH_OK)
  {
    fprintf(stderr, "File transfer failed: %s\n", ssh_get_error(session));
    ssh_disconnect(session);
    ssh_free(session);
    exit(5);
  }

  // Run a remote command and return the response
  if (commandstr[0]!=0)
  {
    rc=runcommand(session, commandstr);

    if (rc!=SSH_OK)
      fprintf(stderr, "Failed to run remote command: %s\n", ssh_get_error(session));
  }

  if (verbose==1)
    fprintf(stderr, "Disconnecting\n");

  ssh_disconnect(session);
  ssh_free(session);

  return 0;
}
