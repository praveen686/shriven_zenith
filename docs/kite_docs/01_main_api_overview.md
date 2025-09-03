# Kite Connect API - Main Overview

## Key Overview
- Kite Connect is a REST-like HTTP API for stock market investment and trading platforms
- Supports real-time order execution, portfolio management, and live market data streaming
- Requires a Zerodha trading account with 2FA TOTP enabled

## Authentication Requirements
- Developers must create an account at Kite Connect Developer Portal
- Obtain `api_key` and `api_secret`
- Configure a redirect URL for user authentication
- "An `api_key` + `api_secret` pair is issued and you have to register a redirect url"

## Technical Specifications
- Inputs are form-encoded parameters
- Responses are JSON (with potential Gzipping)
- Uses standard HTTP status codes
- API endpoints cannot be directly called from browsers

## Getting Started Steps
1. Create Zerodha trading account
2. Set up developer account
3. Create app and get API credentials
4. Complete authentication flow
5. Choose appropriate SDK
6. Explore documentation and examples

## WebSocket Streaming Overview
- Part of Kite Connect API for real-time market data
- Allows streaming live market information
- Requires authentication through API credentials

## Key Implementation Details
- WebSocket endpoint is referenced in the navigation menu under "WebSocket streaming"
- Full technical implementation details are not present in this introductory page
- Requires prior authentication using `api_key` and `api_secret`

## Authentication Requirements for WebSocket
- Must have a Zerodha trading account
- 2FA TOTP must be enabled
- Requires obtaining API credentials through developer portal
- "The authentication flow is crucial for security"

## Limitations
- API endpoints are "not cross site request enabled"
- Cannot be directly called from browsers
- Requires a backend for handling authentication, especially for mobile/desktop apps

## Recommended Resources
- Official SDK libraries
- Developer Community Forum
- Detailed login flow documentation