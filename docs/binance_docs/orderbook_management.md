# How to Manage a Binance Local Order Book Correctly

## Key Concepts

### Update ID Tracking
- **lastUpdateId**: From REST snapshot - represents the last update included in snapshot
- **U**: First update ID in a depth event
- **u**: Final/last update ID in a depth event  
- **pu**: Previous update ID (should match previous event's `u`)

## Implementation Steps

### 1. Initial Connection
```
wss://stream.binance.com:9443/ws/btcusdt@depth
OR for futures:
wss://fstream.binance.com/stream?streams=btcusdt@depth
```

### 2. Buffer Events
- Store all incoming depth events in a queue
- DO NOT process them yet

### 3. Get REST Snapshot
```
https://api.binance.com/api/v3/depth?symbol=BTCUSDT&limit=1000
```
Returns:
```json
{
  "lastUpdateId": 12345,
  "bids": [["100.00", "5.0"]],
  "asks": [["101.00", "3.0"]]
}
```

### 4. Synchronization Rules

#### Initial Sync
1. Drop all buffered events where `u` < snapshot's `lastUpdateId`
2. First processed event MUST satisfy:
   - `U <= lastUpdateId + 1` AND
   - `u >= lastUpdateId + 1`
3. If no event satisfies this, get new snapshot and restart

#### Continuous Updates
- Each new event's `pu` (previous update) MUST equal previous event's `u`
- If this breaks, order book is out of sync - restart from step 3

### 5. Apply Updates
```python
for each price level in event:
    if quantity == 0:
        remove price level from order book
    else:
        update/insert price level with new quantity
```

## Critical Implementation Notes

### 1. Quantity is Absolute
- The quantity in updates is the NEW total, not a delta
- Always replace existing quantity entirely

### 2. Removing Non-Existent Levels
- It's NORMAL to receive remove (qty=0) for non-existent price levels
- Simply ignore these

### 3. Race Condition Handling
- Event might arrive before REST response
- Always buffer events until snapshot is processed

### 4. Reconnection
- On disconnect, clear order book
- Start fresh from step 1

## Difference: Partial Book vs Diff Depth

### Partial Book Streams (depth5, depth10, depth20)
- Complete snapshot of top N levels
- Contains `lastUpdateId` field
- Replace entire order book with this data
- Simpler but less efficient for deep books

### Diff Depth Stream (depth)
- Incremental updates only
- Contains `U`, `u`, sometimes `pu` fields
- Must maintain local book and apply deltas
- More complex but efficient

## Common Mistakes to Avoid

1. **Processing events before snapshot** - Leads to incomplete book
2. **Not checking U/u continuity** - Missing updates
3. **Treating quantity as delta** - It's absolute!
4. **Not buffering during snapshot fetch** - Lost events
5. **Not handling remove of non-existent levels** - Unnecessary errors

## Example Synchronization Check

```python
last_u = snapshot['lastUpdateId']

for event in buffered_events:
    # Skip old events
    if event['u'] < last_u:
        continue
    
    # First event must overlap with snapshot
    if first_event and not (event['U'] <= last_u + 1 and event['u'] >= last_u + 1):
        return "Out of sync - restart"
    
    # Continuous events must be sequential
    if 'pu' in event and event['pu'] != last_u:
        return "Gap detected - restart"
    
    apply_update(event)
    last_u = event['u']
```

## Best Practices

1. Log all update IDs for debugging
2. Implement health checks (spread should be positive)
3. Monitor for stale data (timestamp checks)
4. Have automatic recovery mechanism
5. Track metrics: sync failures, gaps, latency