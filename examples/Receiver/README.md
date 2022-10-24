# Receiver

Sample client for WinSockRaw that sniffes on the given interface and prints out the packet in hex format.

## Usage

```
Receiver.exe <n_packets> [interface_idx]
```

If no `<interface_idx>` is specified, the receiver will automatically bind to `any`.
The client will receive `n_packets` frames and print them to stdout.

*Notice*: `WinSockRawRecv` is blocking so the client might hang while waiting for frames.