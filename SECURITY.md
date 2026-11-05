# Security

## Reporting a vulnerability

Please do not open a public issue for a security problem. Use GitHub's private reporting instead: the
**Report a vulnerability** button on this repository's **Security** tab. Include what you found, how to
reproduce it, and which tag or commit it affects. You will get an answer, and credit if you want it,
once a fix is ready.

## What the viewer exposes

The viewer is a teaching program, built for trusted machines and networks. Know what it opens before
running it anywhere else:

- `--set serve=PORT` (TCP) and `--set serveWs=PORT` (WebSocket) share the live streams with any client
  that connects. There is **no authentication and no encryption**. The WebSocket endpoint accepts pages
  from local origins only, unless more are listed with `--set wsOrigins`.
- `--set metricsPort=PORT` serves metrics and a diagnostic bundle on localhost. The bundle includes the
  command line, environment and layout.
- Process plugins run as your user. `--set sandbox=1` runs them in Bubblewrap, with no network and no
  home directory, but the rest of the file system is readable. Only load plugins you trust.

Reports about these known limits are still welcome if you find a way past what is described here.
