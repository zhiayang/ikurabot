## ikurabot ##

### what is this ###
A twitch bot. not very exciting, i know.


### why is this ###
For fun, of course.


### building ###
```
$ make
```
Dependencies: C++17, OpenSSL


### how to use this ###
First, setup a `config.json` (see the bottom of this file for a sample). Then, run
the bot with the path to the config, and the path to the database. Since there is no
database (obviously), use `--create` to make one at the designated path:
```
$ ./ikurabot config.json, database.db --create
```


### license ###
The bot itself is licensed under the Apache License (version 2.0) -- see the [LICENSE](LICENSE) file. For ease of building, several
small dependencies are included in this repo (under the `external` folder), under their respective licenses:

1. [tsl robin_map](https://github.com/Tessil/robin-map), MIT
2. [utf8proc](https://github.com/JuliaStrings/utf8proc), MIT
3. [kissnet](https://github.com/Ybalrid/kissnet), MIT
4. [picojson](https://github.com/kazuho/picojson), BSD


### sample config ###

```javascript
{
  "global": {
    // the port on which to run the admin console. this will only
    // listen on localhost, nothing else. use 0 to disable.
    "console_port": 42069
  },

  "twitch": {
    // the username for the bot
    "username": "asdf",

    // the oauth token for the bot's account
    "oauth_token": "123456789abcdefgh",

    // the username of the bot's owner; the owner always run all
    // commands, regardless of their badges in the deployed channel
    "owner": "kami_sama",

    // a list of channel objects
    "channels": [
      {
        // the name of the channel (without the #)
        "name": "<channel name>",

        // true if the bot should ignore pings and commands
        "lurk": false,

        // true if the bot is a moderator in this channel (concerns rate limiting)
        "mod": true,

        // the prefix to use for a command (eg. !eval 1+1). if this is empty, then
        // commands will not be available on this channel.
        "command_prefix": "!",

        // true if the bot should reply to mentions in chat with a markov response.
        "respond_to_pings": true
      },
    ]
  }
}
```
