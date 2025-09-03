# Kite Connect Market Quotes and Instruments Documentation

## Key Endpoints

### 1. Instruments API
- Provides CSV dump of tradable instruments
- Generated daily, covers all exchanges
- **Recommendation**: Download once per day and store locally

### 2. Market Quotes Endpoints
- **`/quote`**: Full market data snapshot (up to 500 instruments)
- **`/quote/ohlc`**: OHLC + Last Price (up to 1000 instruments)
- **`/quote/ltp`**: Last Traded Price (up to 1000 instruments)

## Important Quote Attributes
- Last price
- Volume
- Average price
- Buy/sell quantities
- Open interest
- Market depth
- OHLC (Open, High, Low, Close) data

## Authentication Requirements
- Requires `X-Kite-Version: 3` header
- Requires authorization token with API key and access token:
  ```
  Authorization: token api_key:access_token
  X-Kite-Version: 3
  ```

## Rate Limits
- **`/quote`**: 500 instruments per request
- **`/quote/ohlc`**: 1000 instruments per request
- **`/quote/ltp`**: 1000 instruments per request

## Important Note
**For realtime streaming market quotes, use the WebSocket API.**

## Usage Recommendations
1. Download instrument list daily and cache locally
2. Use appropriate endpoint based on data requirements
3. Consider WebSocket streaming for real-time data
4. Respect rate limits to avoid API throttling