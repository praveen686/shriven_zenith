#!/usr/bin/env python3
import websocket
import json
import time

def on_message(ws, message):
    data = json.loads(message)
    print(f"Received: {json.dumps(data, indent=2)}")
    # Exit after receiving a few messages
    global msg_count
    msg_count += 1
    if msg_count >= 5:
        ws.close()

def on_error(ws, error):
    print(f"Error: {error}")

def on_close(ws):
    print("WebSocket closed")

def on_open(ws):
    print("WebSocket opened")
    # Try subscribing if using /ws endpoint
    # subscribe_msg = {
    #     "method": "SUBSCRIBE",
    #     "params": ["btcusdt@depth5@100ms"],
    #     "id": 1
    # }
    # ws.send(json.dumps(subscribe_msg))

# Test direct stream connection (no subscription needed)
url = "wss://stream.binance.com:9443/ws/btcusdt@depth5@100ms"
print(f"Connecting to: {url}")

msg_count = 0
ws = websocket.WebSocketApp(url,
                            on_message=on_message,
                            on_error=on_error,
                            on_close=on_close,
                            on_open=on_open)
ws.run_forever()