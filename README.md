# Fernschreiber
A Telegram client for Sailfish OS

## Author
Sebastian J. Wolf [sebastian@ygriega.de](mailto:sebastian@ygriega.de)

## License
Licensed under GNU GPLv3

## Build
Simply clone this repository and use the project file `harbour-fernschreiber.pro` to import the sources in your SailfishOS IDE. To build and run Fernschreiber or an application which is based on Fernschreiber, you need to create the file `harbour-fernschreiber/src/tdlibsecrets.h` and enter the required constants in the following format:

```
#ifndef TDLIBSECRETS_H
#define TDLIBSECRETS_H
const char TDLIB_API_ID[] = "42424242";
const char TDLIB_API_HASH[] = "1234567890abcdef1234567890abcdef";
#endif // TDLIBSECRETS_H
```

You get the Telegram API ID and hash as soon as you've registered your own application on [https://my.telegram.org](https://my.telegram.org).

## Credits
This project uses
- The Telegram Database Library (TDLib) - available on [GitHub.com](https://github.com/tdlib/td). Thanks for making it available under the conditions of the Boost Software License 1.0! Details about the license of TDLib in [its license file](https://github.com/tdlib/td/blob/master/LICENSE_1_0.txt).
