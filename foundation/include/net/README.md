# foundation::net: TCP sockets

A small wrapper over the system's sockets. It takes `std::byte` spans, and no header names a platform type. Only
`src/net/SocketWin32.cpp` touches Winsock, and another platform adds one source file beside it.

```cpp
std::expected<NetworkSession, SocketResult> network = NetworkSession::Start();
std::expected<TcpSocket, SocketResult> listener = OpenLoopbackListener(0);
const int32_t port = LocalPort(*listener);
```

## The rules

- Keep a `NetworkSession` alive while a socket exists. Sessions count, so each user can keep its own.
- Every socket is non-blocking after it exists. `Send` and `Receive` return `Transferred`, `WouldBlock` or `Closed`.
  `ConnectLoopback` alone blocks, and only until the connection exists or fails.
- Use `Poll` to wait. It takes up to `k_MaxPollEntries` entries. An entry that wants neither read nor write still reports
  `Failed`, so a caller that has paused its reads still sees a hangup.
- Nagle's algorithm is off on every connection, because these sockets carry small requests and replies.

## A trap in Send

Winsock accepts a non-blocking send of **any size** while its send buffer has room. So a caller that passes its whole
backlog to `Send` moves it into kernel memory, and its own count of unsent bytes never grows. A caller that limits its
backlog must also limit the size of each send. `agent::ToolServer` sends at most 64 KiB per call. We found this with a
test: 200 replies of 1 MiB each all "sent" with the client reading nothing.

## What is not here

These wait until something needs them:

- **UDP.**
- **A listener on another address.** Only the loopback address exists, so that a tool port cannot reach the network by
  accident. Binding another address must be a separate, deliberate function.
- **A POSIX source file.** Nothing here could build or run it yet.
- **Async I/O.** `Poll` reports readiness: it says that a socket is ready, and then the caller does the I/O. An async task
  can sit on a thread that polls. IOCP works the other way: the caller starts the I/O, and the system reports the
  completion. An IOCP path would be a second backend, not a layer on top of this API.
