# image-filtering-on-the-web
Build a small webserver that allows users to run the image filters using communication between processes with sockets.
Run a local server that accepts clients through listening on a socket.
Parse strings and read data from a buffer to get HTTP requests.
Do input validation on bitmap files by checking for read and executable permissions on uploaded files.
Read bitmap data uploaded by a client from socket into a buffer and write the data to a file.

Usage:
- Type "make" on command line to compile files.
- > ./image_server
- Open up a web browser (Chrome or Firefox) and enter: http://localhost:59589/main.html
