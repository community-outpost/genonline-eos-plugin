# Generals Online EOS plugin

`genonline_eos`: Easy Anti-Cheat client, EOS P2P transport and EOS Connect linked account.

## Requirements

- `genonline-plugin-sdk`, by default in `../genonline-plugin-sdk`.
- EOS SDK 1.18.1 `Include`, `Lib` and `Bin` in `EOS/`, from the Epic developer portal.
- A C++17 compiler targeting 32-bit Windows and CMake 3.19+.

## Build

```sh
cmake -S . -B <build> -A Win32
cmake --build <build> --config Release
ctest --test-dir <build> -C Release --output-on-failure
```

Any CMake generator works; with a single-config one (Ninja) use `-DCMAKE_BUILD_TYPE=Release` instead of `-A Win32` and `--config`.

`<build>/plugins/genonline_eos/` then holds `genonline_eos.dll`, `EOSSDK-Win32-Shipping.dll` and the unsigned `plugin.manifest`. Sign it as the SDK describes.

| Cache variable | Default |
|---|---|
| `GENONLINE_PLUGIN_SDK_DIR` | `../genonline-plugin-sdk` |
| `GENONLINE_EOS_SDK_DIR` | `EOS` |
| `GENONLINE_EOS_VERSION` | `2026.11.0` |
| `GENONLINE_EOS_PRODUCT_ID`, `_SANDBOX_ID`, `_DEPLOYMENT_ID`, `_CLIENT_ID`, `_CLIENT_SECRET` | empty |

Without credentials `Initialize` returns `GENONLINE_PLUGIN_RESULT_MISSING_CREDENTIALS`.

## EOS Developer Portal setup

In the product's **Product Settings**:

1. **Identity Providers**: add an **OpenID** provider using **JWKS**. Set the JWKS URL of your Generals Online service, the expected audience (`aud`) of its EOS token and the claim that holds the display name. The account id is the token's `sub`.
2. **Sandboxes** > **Identity Providers**: enable that provider in the sandbox the build uses.
3. **Clients**: add a client policy, either the predefined **Peer2Peer** or a custom one with the P2P features and Anti-Cheat `authenticateForLocalUser`, with **User required** on. Then add a client with that policy. Its client id and secret go into the build.
4. **SDK Download & Credentials**: the product, sandbox and deployment ids.

For Easy Anti-Cheat, the organization must sign the Anti-Cheat agreement. Each release then needs an integrity catalog made with the product's integrity keys, and the game is started through the EAC bootstrapper.

See [Identity Provider Management](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/identity-provider-management) and [Client and Client Policy](https://dev.epicgames.com/docs/epic-online-services/eos-fundamentals/client-and-client-policy).
