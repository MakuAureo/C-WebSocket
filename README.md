# C-WebSocket
This repository was made this over a week because I wanted to learn both how WebSockets work and how C memory management works, I'm satisfied with how it turned out, but I'm not done with the whole project just yet.

# Compiling
`gcc src/*.c -Iinclude -lssl -lcrypto`

# Future plans
- Add more options for injecting behavior in the event loop
- Add multi-threading with a accept and worker threads
