# Kite Connect Orders API Documentation

## Overview
- Allows placing, modifying, canceling, and retrieving orders
- Supports multiple order varieties and exchanges

## Key Endpoints
- **POST** `/orders/:variety` - Place an order
- **PUT** `/orders/:variety/:order_id` - Modify an order
- **DELETE** `/orders/:variety/:order_id` - Cancel an order
- **GET** `/orders` - Retrieves all day's orders
- **GET** `/orders/:order_id` - Retrieves specific order history
- **GET** `/trades` - Retrieves all executed trades

## Order Varieties
- Regular
- After Market Order (AMO)
- Cover Order (CO)
- Iceberg Order
- Auction Order

## Important Order Parameters
- `tradingsymbol`
- `exchange`
- `Transaction_type` (BUY/SELL)
- `order_type` (MARKET, LIMIT, SL)
- `product` (CNC, NRML, MIS)
- `quantity`
- `price`
- `validity`

## Key Features
- Market protection
- Auto-slice orders
- Multi-legged orders
- Order tagging
- Detailed order status tracking

## Order Status Examples
- "OPEN"
- "COMPLETE"
- "CANCELLED"
- "REJECTED"

## Authorization
All endpoints require Authorization header:
```
Authorization: token api_key:access_token
```

The API provides comprehensive order management with flexible configuration options across multiple exchanges.