# Step 7: predictions (written before the code exists)

## The feature

- **A second viewer in another process** (later, another machine): the same viewer started with `--set connect=host:port` and no simulator. It shows the live lidar, tracks and events of the first. Cameras stay local: streaming video needs re-encoding, which is out of scope here.
- **On the first viewer:** a "Clients" section in the lab panel lists connected viewers with their send rate, queue and drops.
- **On the second viewer:** its health panel shows the network streams. Its timeline and recording work as on the first, because the network is just another source of samples.

## The wire

Step 4's record format, over TCP: a header (format version, JSON info), then one record per sample (type, version, stream, capture time, size, payload). Lidar travels as planar int16, optionally zstd-compressed per record. Encoded once, then sent to every client.

## The design questions

One process or several, and what crosses the boundary? What happens when the receiver can't keep up?

## The first implementation

Write every record to every client's socket as it arrives (`QTcpSocket::write`).

## Guesses

| Question | Guess |
|---|---|
| Bandwidth per client at nominal: float32 / int16 / int16 + zstd | 72 / 36 / 22 MB/s (loopback handles all three) |
| End-to-end latency, capture → photon on the second viewer (same machine) | Local + 5–15 ms (encode, loopback, decode) |
| Server CPU per extra client, encoding once | +1–3% (copies into socket buffers) |
| Server CPU per client, encoding per client | +8–10% each (zstd) |
| A client that reads at 10 MB/s, first implementation | The server's write buffer grows without bound (`bytesToWrite`): ~12 MB/s more memory; the client falls further behind every second, tens of seconds after a minute |
| Same, with latest-wins per stream and the socket's backlog as the limit | Server memory flat; that client's lidar latency bounded (a few hundred ms) at a lower frame rate; events kept up to a bound, then dropped and counted |
| The slow client's effect on other clients and on the first viewer | First implementation: none on the GUI, but memory grows. With per-client limits: none |
| Client restart or network cut | Client reconnects with backoff; its health shows stale, then ok; server unaffected |
| Clock: is capture time comparable across the two processes? | Yes on one machine (one monotonic clock). Across machines it would need clock sync (step 3's offset estimation) |
