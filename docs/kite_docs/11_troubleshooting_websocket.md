# Kite Connect WebSocket Troubleshooting Guide

## Common WebSocket Connection Problems

### 1. Connection Error 1006
- **Symptoms**: "Connection was closed uncleanly"
- **Potential Causes**:
  - Authentication issues
  - Network instability
  - API token expiration
  - Invalid access token or API key

### 2. Streaming Data Challenges
Users commonly report difficulties receiving:
- Index/Futures quotes
- Consistent tick data
- Total buy/sell volumes

## Troubleshooting Steps

### Authentication Issues
1. **Verify API credentials**:
   - Check API key format
   - Ensure access token is valid and not expired
   - Confirm access token was generated correctly

2. **Connection URL format**:
   ```
   wss://ws.kite.trade?api_key=YOUR_API_KEY&access_token=YOUR_ACCESS_TOKEN
   ```

### Network and Connection Issues
1. **Check network stability**
2. **Implement robust reconnection logic**
3. **Validate authentication credentials before connection**
4. **Monitor connection state continuously**

### SSL/TLS Requirements
- Use secure WebSocket connection (WSS)
- Ensure proper certificate validation
- Check firewall and proxy settings

## Best Practices for Implementation

### Error Handling
```python
def on_error(ws, code, reason):
    print(f"WebSocket error: {code} - {reason}")
    # Implement reconnection logic
    
def on_close(ws, code, reason):
    print(f"Connection closed: {code} - {reason}")
    # Attempt to reconnect if unexpected closure
```

### Reconnection Strategy
```python
import time

def reconnect_with_backoff(max_retries=5):
    for attempt in range(max_retries):
        try:
            kws.connect()
            break
        except Exception as e:
            wait_time = 2 ** attempt
            print(f"Reconnection attempt {attempt + 1} failed, waiting {wait_time}s")
            time.sleep(wait_time)
```

### Connection Validation
1. Test connection in controlled environment
2. Implement comprehensive error logging
3. Monitor connection health with heartbeat checks
4. Validate data integrity of received ticks

## Common Solutions

### Token Expiration
- Implement automatic token refresh
- Handle TokenException gracefully
- Store token expiry time and refresh proactively

### Network Interruptions
- Implement exponential backoff for reconnection
- Use connection pools for better reliability
- Handle temporary network failures gracefully

### Data Quality Issues
- Validate received tick data
- Implement data consistency checks
- Log anomalies for debugging

## Debugging Tips
1. **Enable verbose logging** for WebSocket events
2. **Monitor network traffic** during connection attempts
3. **Test with minimal instrument subscriptions** first
4. **Verify system time synchronization**
5. **Check for rate limiting issues**

## When to Contact Support
- Persistent authentication failures despite valid credentials
- Consistent data quality issues
- Unexplained connection drops across multiple environments