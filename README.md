# raft
raft - Remote Administration File Transfer

raft is intended for scripting or command line remote administration and file transfers.

You can specify either a remote command to run, a file transfer to take place (only local to remote), or a local to remote transfer followed by a command. So for instance you can transfer a script file to a remote host, run that script and collect any output.

When local files are transferred to a remote host, raft will attempt to preserve the permissions, owner and timestamps.

To get around issues with escaping command lines related to quotes and characters which get interpretted incorrectly, you can base64 encode the command line and use the `-c64` option instead of `-c`.

For example, the `ls -l` command can be bas64 encoded with : `echo -n "ls -l" | base64`, which results in `bHMgLWw=` which becomes the parameter for the `-c64` option.

## Syntax :

`raft [-h [username@]remotehost] [-i identityfile] [-p password] [-c "shell command"] [-c64 "base64 encoded shell command"] [-l localfilename] [-r remotefilename] [-z] [-v]`

## Where :

 * `-h` Remote hostname with optional username, if not specified currently logged in username is used
 * `-i` Specify the use of an identity file (e.g. id_rsa)
 * `-p` Optional password to use, or passphrase for identity file if used
 * `-c` Execute a command on the remote host, the response is returned
 * `-c64` Execute a base64 encoded command on the remote host, the response is returned
 * `-l` Local filename to transfer to remote host
 * `-r` Remote filename to use when transferring a local file
 * `-z` Use compression (level 9)
 * `-v` Verbose

## Return codes :

 * `0` - Success
 * `1` - Error creating SSH session
 * `2` - Error connecting to remote host
 * `3` - Error importing identity file
 * `4` - Error authenticating with password
 * `5` - Error file transfer failed
 
## Requirements :
 
 * OpenSSL libraries
