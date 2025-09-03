# Kite Connect WebSocket Streaming Documentation

## WebSocket Endpoint Details
- **URL**: `wss://ws.kite.trade`
- **Protocol**: WebSocket Secure (WSS)
- **Authentication Parameters**:
  - Required query parameters: `api_key` and `access_token`

## Connection Limits
- Up to 3000 instruments per connection
- Single API key can have up to 3 WebSocket connections

## Request Structure
JSON messages with two parameters:
- `a` (action)
- `v` (value)

## Supported Actions
- `subscribe`: Add instrument tokens
- `unsubscribe`: Remove instrument tokens
- `mode`: Set data streaming mode

## Streaming Modes
1. **`ltp`**: Last traded price only (8 bytes)
2. **`quote`**: Multiple fields excluding market depth (44 bytes)
3. **`full`**: Complete data including market depth (184 bytes)

## Key Technical Characteristics
- Binary message format for market data
- Text messages for postbacks/updates
- Heartbeat signals to maintain connection
- Supports multiple packet types in single message

## Message Types
- `order`: Order updates
- `error`: Error responses
- `message`: Broker alerts

## Recommended Implementation
- Use pre-built client libraries
- Implement asynchronous WebSocket client
- Parse binary market data carefully
- Always validate incoming message types and parse accordingly

## Connection Format
```
wss://ws.kite.trade?api_key=YOUR_API_KEY&access_token=YOUR_ACCESS_TOKEN
```