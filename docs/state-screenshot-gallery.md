# Claudy State Screenshot Gallery

Captured from the ESP32-S3 `/screenshot.bmp` endpoint at 320x170.

All examples use neutral placeholder state data. Replace `<claudy-ip>` with
your device IP when reproducing locally.

## Idle

![Idle](state-screenshots/idle.bmp)

## Thinking

![Thinking](state-screenshots/thinking.bmp)

## Working

![Working](state-screenshots/working.bmp)

## Waiting

![Waiting](state-screenshots/waiting.bmp)

## Error

![Error](state-screenshots/error.bmp)

## Done

![Done](state-screenshots/done.bmp)

## Reproduce

Use the ESP32-S3 screenshot endpoint after posting a state:

```bash
curl -X POST http://<claudy-ip>/state \
  -H 'Content-Type: application/json' \
  -d '{"state":"working","tool":"Bash","message":"npm run test","client":"codex-vscode","model":"gpt-5.5","tokens":{"used":86000,"max":200000}}'

curl http://<claudy-ip>/screenshot.bmp -o docs/state-screenshots/working.bmp
```
