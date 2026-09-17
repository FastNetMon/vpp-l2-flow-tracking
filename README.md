# vpp-l2-flow-tracking

VPP 26.06 plugin (`l2flow`) that adds SFDP-based stateful flow tracking to an
L2 cross-connect setup, without changing how packets are forwarded.

## How it works

```
device-input arc                     SFDP service chain
┌──────────────┐   IPv4/IPv6   ┌─────────────────┐    ┌───────────────┐
│ l2flow-input ├──────────────▶│ sfdp-lookup-ip4 ├───▶│ l2flow-output │
└──────┬───────┘               │ sfdp-lookup-ip6 │    └───────┬───────┘
       │ non-IP / disabled     └─────────────────┘            │
       ▼                                                      ▼
┌────────────────┐                                    ┌────────────────┐
│ ethernet-input │◀───────────────────────────────────┤ back to L2 path│
└──────┬─────────┘                                    └────────────────┘
       ▼
   l2-input → xconnect → interface-output
```

- `l2flow-input` is a `device-input` feature node that runs before
  `ethernet-input`. Untagged IPv4/IPv6 packets on enabled interfaces are
  assigned the configured SFDP tenant and steered into `sfdp-lookup-ip4/ip6`,
  which creates/matches bidirectional flow sessions.
- `l2flow-output` is a terminal SFDP service that rewinds the buffer to the
  ethernet header and re-injects it into `ethernet-input`, so the existing
  xconnect forwards the packet exactly as before.
- Non-IP traffic (ARP, etc.) and tagged traffic bypass SFDP and follow the
  plain xconnect path.

## Configuration

Your existing L2 setup, plus SFDP flow tracking:

```
set interface state eth0 up
set interface state eth1 up
set interface l2 xconnect eth0 eth1
set interface l2 xconnect eth1 eth0

sfdp tenant add 1 context 1
set sfdp services tenant 1 sfdp-l4-lifecycle l2flow-output forward
set sfdp services tenant 1 sfdp-l4-lifecycle l2flow-output reverse

set l2flow interface eth0 tenant 1
set l2flow interface eth1 tenant 1
```

Inspect flows:

```
show sfdp session-table
show l2flow interfaces
show sfdp services
```

Disable on an interface:

```
set l2flow interface eth0 disable
```

## Building

The plugin builds in-tree against VPP `stable/2606`:

```sh
git clone --depth 1 --branch stable/2606 https://github.com/FDio/vpp.git
cp -r src/l2flow vpp/src/plugins/l2flow
cd vpp
make install-dep install-ext-deps
make build-release
```

Fast path (plugin only, no ext-deps/DPDK needed):

```sh
cmake -S vpp/src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target l2flow_plugin
# -> build/lib/*/vpp_plugins/l2flow_plugin.so
```

The resulting binary is
`build-root/install-vpp-native/vpp/lib/*/vpp_plugins/l2flow_plugin.so`.

CI: the GitHub Actions workflow in
[.github/workflows/build.yml](.github/workflows/build.yml) builds the plugin on
every push/PR and uploads `l2flow_plugin.so` as an artifact.

## Notes

- Requires the `sfdp_services` plugin to be enabled (provides
  `sfdp-l4-lifecycle`, `sfdp-tcp-check`, ...).
- Flow tracking is IPv4/IPv6-only by design; SFDP sessions are keyed on the
  IP 5-tuple.
