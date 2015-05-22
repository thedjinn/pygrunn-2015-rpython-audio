# RPython Audio Graph Renderer

This repository contains the supporting code for my talk at PyGrunn 2015 about
audio rendering using RPython.

## How to use?

First, download the latest PyPy source code and put it into a directory named
"pypy" in the root of this repository. Also ensure that the `pypy` binary is
in the path. Then run the `c` script to start translating the RPython code and
grab a coffee because it will take a while.  When it's done you can run the
`main-c` executable that was generated and enjoy the music.

Note: the code has been designed to compile on Mac OS X 10.10 and might need
modifications on other platforms.

## License

Copyright (c) 2015 Emil Loer

Permission  is  hereby granted, free of charge, to any person obtaining a copy of  this  software  and  associated  documentation files  (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is  furnished to do so, subject to the following conditions:

The  above  copyright  notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF  ANY  KIND, EXPRESS  OR  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE  AND  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER  IN  AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN  THE SOFTWARE.
