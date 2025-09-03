# Kite Connect Historical Candle Data API Documentation

## API Endpoint
- **Type**: GET
- **Path**: `/instruments/historical/:instrument_token/:interval`

## URI Parameters
- **`instrument_token`**: Unique identifier for the instrument (obtained from instrument list API)
- **`interval`**: Candle record intervals
  - Options: `minute`, `day`, `3minute`, `5minute`, `10minute`, `15minute`, `30minute`, `60minute`

## Request Parameters
- **`from`**: Start date in `yyyy-mm-dd hh:mm:ss` format
- **`to`**: End date in `yyyy-mm-dd hh:mm:ss` format
- **`continuous`**: `0` or `1` (get continuous data)
- **`oi`**: `0` or `1` (get Open Interest data)

## Response Structure
- Array of records
- Each record: `[timestamp, open, high, low, close, volume, (optional) open interest]`

## Key Features
- Provides historical market data across exchanges
- Supports granular time interval retrieval
- Offers continuous data for futures and options contracts

## Authentication
Requires authorization header with API key and access token:
```
Authorization: token api_key:access_token
X-Kite-Version: 3
```

## Example Request
```bash
curl "https://api.kite.trade/instruments/historical/5633/minute?from=2017-12-15+09:15:00&to=2017-12-15+09:20:00" \
  -H "Authorization: token api_key:access_token" \
  -H "X-Kite-Version: 3"
```

## Unique Feature
Supports retrieving historical data for expired contracts using the `continuous` parameter.