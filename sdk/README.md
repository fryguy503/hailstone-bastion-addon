# Public Hailstone SDK snapshot

These MIT-licensed headers match the canonical shared SDK at commit
`c55bfccd325d0abc77f98fc28eb8cf33c24b3858`, tree `166baf23`.
The shared work is tracked in
[draft PR #82](https://github.com/projecthailstone/hailstone-client/pull/82).
They expose no private client implementation.

`hailstone_plugin.h` SHA256: `673a348cad651775ae74741a718cd5c3f0230a035e1cafa3eaabe640aa4eb9d6`.
The canonical ABI 3 table has 106 members, including 103 function pointers, and is 840 bytes on Windows x64.
Its Canonical 2 layout ID is `0xe7faba7b569803ce`. Five released bulk-reader slots precede the draft services;
`get_map_policy` is member 106. Sizes still gate optional services after layout negotiation.

The host update supplies follow profiles, observed raid state and guarded actions, item-packet
inspection, merchant-click context, map eligibility and ownership, item metadata/loot warnings, current-instance
zone names and terrain-aware world geometry with actual presentation receipts.
It also measures the actual executable and the caller's admitted loaded plugin copy for native
integrity protocols, without accepting arbitrary paths or exposing hardware identifiers.

Bundling headers makes standalone addon builds reproducible. It does not add these services to an
older host. The addon checks the host table before activating on a selected Bastion peer.
Map ownership also requires the extended revision/context record and a nonzero host-issued
world context. A prefix-only reader can preserve its caller's buffer size, so size alone is
not used as proof of support. The original policy prefix remains compatible for older providers.

The addon manually exports `hailstone_plugin_layout` and `hailstone_plugin_set_host_v2`, with the same
layout and minimum-prefix checks as the SDK macro. It exports no unversioned host setter. An older C5 client
therefore refuses this canonical DLL before host binding or `InitializePlugin`; operating-system module loading
can still run static constructors first. Loading an experimental unmarked preview DLL in the new client requires an explicit
`ui-preview-1` override tied to that DLL's exact SHA256. Rebuilding with this SDK is preferred; ABI 3 alone
does not identify the incompatible preview tail.

The prior marked Canonical 1 layout (`0xf6b34b37061be009`) has a distinct 832-byte compatibility view
in the matching client. It needs no hash override. This DLL advertises Canonical 2; an older F6-only
host refuses it before binding. The experimental unmarked `ui-preview-1` layout remains a separate
exact-SHA256 opt-in. Never hand either older layout the current 840-byte table.
