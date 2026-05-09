# wifip2p — wire protocol

## Stage 1 — line-based text (`WIFIP2P-DEMO/1`)

Transport: TCP, port `8888`, plain UTF-8 bytes, lines terminated by `\n`
(server tolerates trailing `\r`).

Server (Windows GO) opens by sending:

```
WIFIP2P-DEMO/1\n
```

Then it loops, reading one line at a time:

| Client → Server         | Server → Client            |
|-------------------------|----------------------------|
| `HELLO <name>\n`        | `WELCOME <name>\n`         |
| `PING\n`                | `PONG\n`                   |
| anything else (`<x>\n`) | `ECHO <UPPERCASED(x)>\n`   |

Closing the socket on either side terminates the session.

## Stage 2 — raw H.264 NAL stream (planned, not implemented)

Same TCP socket. After `HELLO`/`WELCOME` handshake the client may send:

```
START-VIDEO width=<w> height=<h> fps=<f>\n
```

Then the byte stream switches to length-prefixed NAL units:

```
[uint32 BE: nal_byte_length] [nal_byte_length bytes of raw NALU starting with 0x00 00 00 01 start code]
```

Annex-B start codes are kept inside the prefixed payload so the receiver
can hand the buffer straight to MediaFoundation's H264 decoder.
