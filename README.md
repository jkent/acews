# Readme: ACEWS Library
## Another C Embedded Web Server

Why another embedded web server? libesphttpd is hard to use.  esp\_http\_server
has a weird callback system.  Why not try to make a low memory footprint web
server supporting HTTP/1.1 and websockets, with an API that that can eventually
support HTTP/2 and HTTP/3, yet remain simple to use?  Thus ACEWS was born.

You can find [the Linux demo project here](https://github.com/jkent/acews-linux-demo).
