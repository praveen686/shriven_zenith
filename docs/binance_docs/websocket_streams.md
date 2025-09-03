# Binance WebSocket Streams Documentation

## Base Endpoints
- **Production**: `wss://stream.binance.com:9443` or `wss://stream.binance.com:443`
- **Market Data Only**: `wss://data-stream.binance.vision`

## Connection Limits
- Valid for 24 hours; expect disconnection after 24 hours
- Server sends ping frame every 20 seconds
- Max 5 incoming messages per second per connection
- Max 1024 streams per single connection
- 300 connections per 5 minutes per IP

## Stream Access Methods

### 1. Raw Single Stream
```
wss://stream.binance.com:9443/ws/<streamName>
```

### 2. Combined Streams
```
wss://stream.binance.com:9443/stream?streams=<streamName1>/<streamName2>/<streamName3>
```

## Order Book / Depth Streams

### Partial Book Depth Streams
Top <levels> bids and asks, pushed every second.

**Stream Names**: 
- `<symbol>@depth<levels>` where levels = 5, 10, or 20
- `<symbol>@depth<levels>@100ms` for 100ms updates

**Examples**:
- `btcusdt@depth5` - Top 5 levels
- `btcusdt@depth10@100ms` - Top 10 levels, 100ms updates

**Payload**:
```json
{
  "lastUpdateId": 160,  // Last update ID
  "bids": [
    ["0.0024", "10"],  // [price, quantity]
    ["0.0023", "100"]
  ],
  "asks": [
    ["0.0026", "100"],
    ["0.0027", "40"]
  ]
}
```

### Diff. Depth Stream
Order book price and quantity depth updates.

**Stream Name**: `<symbol>@depth` or `<symbol>@depth@100ms`

**Payload**:
```json
{
  "e": "depthUpdate",   // Event type
  "E": 123456789,       // Event time
  "s": "BNBBTC",        // Symbol
  "U": 157,             // First update ID in event
  "u": 160,             // Final update ID in event
  "b": [                // Bids to be updated
    ["0.0024", "10"]    // [price, quantity]
  ],
  "a": [                // Asks to be updated
    ["0.0026", "100"]
  ]
}
```

## Managing a Local Order Book

1. Open stream via `wss://stream.binance.com:9443/ws/bnbbtc@depth`
2. Buffer events received over stream
3. Get depth snapshot from REST: `https://api.binance.com/api/v3/depth?symbol=BNBBTC&limit=1000`
4. Drop events where `u` <= `lastUpdateId` from snapshot
5. First processed event should have `U` <= `lastUpdateId+1` AND `u` >= `lastUpdateId+1`
6. Apply updates to price levels:
   - If quantity = 0, remove price level
   - If quantity > 0, update price level
7. Final `u` should be used as `lastUpdateId` for next update

## Subscription via WebSocket

### Subscribe
```json
{
  "method": "SUBSCRIBE",
  "params": [
    "btcusdt@trade",
    "btcusdt@depth10@100ms"
  ],
  "id": 1
}
```

### Unsubscribe
```json
{
  "method": "UNSUBSCRIBE",
  "params": [
    "btcusdt@depth"
  ],
  "id": 2
}
```

### Response
```json
{
  "result": null,
  "id": 1
}
```

## Combined Streams Format

When using combined streams endpoint, messages are wrapped:
```json
{
  "stream": "btcusdt@depth10",
  "data": {
    // actual payload here
  }
}
```

## Important Notes

1. **For partial book streams** (`depth5`, `depth10`, `depth20`):
   - Payload contains `lastUpdateId` field
   - Complete snapshot of top N levels
   - No event type field

2. **For diff depth stream** (`depth`):
   - Payload contains `e: "depthUpdate"` field
   - Incremental updates only
   - Must maintain local order book

3. **Connection Management**:
   - Respond to ping with pong
   - Handle 24-hour disconnection
   - Implement exponential backoff for reconnects

4. **Rate Limits**:
   - WebSocket connections: 300 per 5 minutes
   - Message rate: 5 per second
   - Total streams: 1024 per connection