# Kite Connect Authentication Flow Documentation

## Login Process
1. Navigate to Kite login endpoint: 
   ```
   https://kite.zerodha.com/connect/login?v=3&api_key=xxx
   ```

2. Successful login returns a `request_token` to the registered redirect URL

## Token Exchange Steps
1. **Generate checksum**: SHA-256 of (api_key + request_token + api_secret)
2. **POST** `request_token` and `checksum` to `/session/token`
3. **Receive** `access_token`

## Request Signing
- Use HTTP Authorization header: 
  ```
  Authorization: token api_key:access_token
  ```

## Security Best Practices
- **Never expose** `api_secret` in client-side applications
- **Do not publicly share** `access_token`
- `access_token` automatically expires at 6 AM next day

## Session Management
- Obtain user profile via `/user/profile`
- Logout by calling `DELETE /session/token`
- Logout invalidates current access token

## Key Tokens
- **`request_token`**: Short-lived, used for initial exchange
- **`access_token`**: Used for all subsequent API requests
- **`public_token`**: For public session validation

## WebSocket Authentication
For WebSocket connections, use the `access_token` obtained from the authentication flow:
```
wss://ws.kite.trade?api_key=YOUR_API_KEY&access_token=YOUR_ACCESS_TOKEN
```

## Additional Features
- Optional `redirect_params` can be appended to login endpoint for custom parameter passing