# nvhttpd

A simple web server in C. NVHTTPD only handles GET and HEAD requests. The code is present to parse query parameters and header
fields, but it is not currently used. If you want to experiment with those, you can take out the following code in `request.c`:

```c
    if (request->method != REQUEST_METHOD_GET && request->method != REQUEST_METHOD_HEAD) {
        debug_return REQUEST_PARSE_NOT_IMPLEMENTED;
    }
```

Only static web pages are served in this initial version. The server can be configured in ```nvhttpd.conf```, which is
expected to be in the same directory as the binary. ```nvhttpd.conf``` is commented.

Building is simple: just run ```make``` in the source directory. You can add ```debug=1``` to generate a debug version.
