# Injector

Sample client for WinSockRaw that reads packets from a PCAP and injects them in the specified interface

*Notice*: the PCAP parsing is very lightweight and not robust, no error checks is performed. 

## Usage

```
Injector.exe <interface_idx> <filename>
```

The client will read the frames from `<filename>` pcap file and inject them in `<interface_idx>` interface.

*Notice*: An interface index is required to inject frame, the `any` interface is not supported.