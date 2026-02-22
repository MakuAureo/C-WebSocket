# C-WebSocket
This repository was made over a week because I wanted to learn both how WebSockets and C memory management work, I'm satisfied with how it turned out, but I'm not done with the whole project just yet.

# Compiling
`gcc src/*.c -Iinclude -lssl -lcrypto`

# Future plans
- ~Add more options for injecting behavior in the event loop~
- Add multi-threading with a accept and worker threads
- Add option to handle connection paths with different onHandshake and onConnect callbacks
