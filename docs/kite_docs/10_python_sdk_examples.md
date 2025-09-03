# PyKiteConnect Python SDK Documentation and Examples

## Installation
```bash
pip install --upgrade kiteconnect
pip install -U pip setuptools
```

## Key SDK Features
- Official Python client for Kite Connect API
- Supports trading platform interactions
- Enables order execution, portfolio management, market data streaming

## Authentication Flow
```python
from kiteconnect import KiteConnect

kite = KiteConnect(api_key="your_api_key")

# Generate session after receiving request token
data = kite.generate_session("request_token", api_secret="your_secret")
kite.set_access_token(data["access_token"])
```

## Order Placement Example
```python
order_id = kite.place_order(
    tradingsymbol="INFY",
    exchange=kite.EXCHANGE_NSE,
    Transaction_type=kite.TRANSACTION_TYPE_BUY,
    quantity=1,
    variety=kite.VARIETY_AMO,
    order_type=kite.ORDER_TYPE_MARKET,
    product=kite.PRODUCT_CNC
)
```

## WebSocket Connection Example
```python
from kiteconnect import KiteTicker

kws = KiteTicker("api_key", "access_token")

def on_ticks(ws, ticks):
    print("Ticks received")

def on_connect(ws, response):
    ws.subscribe([738561, 5633])
    ws.set_mode(ws.MODE_FULL, [738561])

def on_close(ws, code, reason):
    print("Connection closed")

def on_error(ws, code, reason):
    print("Error occurred")

kws.on_ticks = on_ticks
kws.on_connect = on_connect
kws.on_close = on_close
kws.on_error = on_error

# Start the WebSocket connection
kws.connect()
```

## WebSocket Modes
```python
# Available modes:
ws.MODE_LTP     # Last traded price
ws.MODE_QUOTE   # Quote data
ws.MODE_FULL    # Full market data
```

## Notable Changes in v5
- Dropped Python 2.7 support
- Renamed some ticker fields
- Recommended using <= 4.x.x for Python 2.x users

## Supported Platforms
- Linux/BSD
- macOS
- Windows (with specific compiler versions)

The SDK provides comprehensive methods for trading, market data, and portfolio management through a straightforward Python interface.