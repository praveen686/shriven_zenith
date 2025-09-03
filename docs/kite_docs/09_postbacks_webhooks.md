# Kite Connect Postbacks/WebHooks Documentation

## Key Characteristics
- Sends `POST` request to registered `postback_url` when order status changes
- Provides updates for order statuses: `COMPLETE`, `CANCEL`, `REJECTED`, `UPDATE`
- Primarily designed for platforms with multiple user orders

## Payload Details
- JSON payload sent as raw HTTP POST body
- Includes comprehensive order information (order ID, exchange, status, quantities, etc.)

## Security Mechanism
- Includes a `checksum` for validation
- **Checksum calculation**: SHA-256 hash of: `order_id` + `order_timestamp` + `api_secret`
- Validates that the update comes from Kite Connect

## Important Notes
- **Recommended for**: Platforms placing orders for multiple users
- **Individual developers**: Advised to use WebSocket for order tracking instead
- Works even when user is not logged in

## Sample Payload Attributes
- `user_id`
- `order_id`
- `status`
- `exchange`
- `quantity`
- `average_price`
- Timestamps for order registration

## Security Best Practices
- **Compute and verify checksum** for each received postback
- Ensure payload is from authorized source
- Never trust postbacks without proper checksum validation

## Checksum Validation
```python
import hashlib

def verify_postback(order_id, order_timestamp, api_secret, received_checksum):
    expected_checksum = hashlib.sha256((order_id + order_timestamp + api_secret).encode()).hexdigest()
    return expected_checksum == received_checksum
```

## Usage Recommendation
For individual developers and simple applications, use WebSocket streaming for real-time order updates instead of postbacks.