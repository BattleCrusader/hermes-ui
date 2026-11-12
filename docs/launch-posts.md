# Launch posts — Hermes UI

Repo: https://github.com/BattleCrusader/hermes-ui
Suggested image: `screenshots/light.png`
Alt text: “Hermes UI native desktop client showing the profile rail, chat transcript, and composer.”

## X

A native desktop client for the Hermes Agent gateway — C++20 + Qt 6 Widgets.

No Electron. No web view.

One warm worker per profile, real WebSocket streaming, instant switching.

Working MVP, open source:
github.com/BattleCrusader/hermes-ui

#Cpp #Qt #AI

## LinkedIn

Most AI agent UIs are web apps wrapped in Electron. I wanted to see what a genuinely native one felt like.

Hermes UI is a C++20 / Qt 6 Widgets desktop client for the Hermes Agent gateway. It speaks the real local gateway protocol: control-socket tickets, REST history, and the `/api/ws` event stream.

What is already working:

- One warm worker per profile — switching is a pointer swap, not a reconnect
- Live token streaming and inline tool calls
- Real sessions and history, new chat, stop
- Approval, clarify, sudo and secret request cards
- Custom-painted 34px title bar, codicon icons, light/dark design tokens
- No QML and no embedded web view in the app shell

It is early and not feature-complete, but it runs real turns against a live gateway.

Code: https://github.com/BattleCrusader/hermes-ui

#Cpp #Qt #AI #OpenSource #DesktopApps
