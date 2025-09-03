# Kite Connect Portfolio API Documentation

## Overview
- Provides access to user's long-term holdings and short-term positions
- Supports retrieving, converting, and managing investment portfolios

## Key Endpoints

### 1. Holdings (`/portfolio/holdings`)
- Retrieves long-term equity holdings
- Returns detailed stock information including:
  - Trading symbol
  - Exchange
  - Quantity
  - Average price
  - Profit/Loss
  - Current market price

### 2. Positions (`/portfolio/positions`)
- Tracks short to medium-term derivatives and intraday stocks
- Provides two position views: 
  - **"Net"** - current portfolio
  - **"Day"** - daily trading activity

### 3. Position Conversion (`/portfolio/positions`)
- Allows changing margin product for existing positions
- Supports converting between products like NRML, MIS

### 4. Holdings Auction List (`/portfolio/holdings/auctions`)
- Lists current stock auctions for held securities

## Notable Features
- Supports electronic holdings authorization
- Provides detailed profit/loss calculations
- Tracks intraday and overnight position changes

## Authentication Requirements
- API key
- Access token
- X-Kite-Version header

```
Authorization: token api_key:access_token
X-Kite-Version: 3
```

The API offers comprehensive portfolio management capabilities for traders and investors.