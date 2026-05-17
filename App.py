
from flask import Flask, request, jsonify, send_file
import io
import os

app = Flask(__name__)

# In-memory storage for the latest audio chunk (bypasses Render's temporary file storage)
latest_audio_packet = b""
packet_id = 0

@app.route('/')
def index():
    return jsonify({
        "status": "online",
        "project": "CBMR (Custom Built Military-esque Radio)",
        "current_packet_id": packet_id
    })

# Endpoint for the Transmitting Radio (TX)
@app.route('/transmit', methods=['POST'])
def transmit():
    global latest_audio_packet, packet_id
    
    # Grab the raw binary bytes sent by the ESP32
    raw_audio = request.data
    
    if not raw_audio:
        return jsonify({"error": "No audio payload received"}), 400
    
    latest_audio_packet = raw_audio
    packet_id += 1
    
    print(f"[TX] Received packet #{packet_id} ({len(raw_audio)} bytes)")
    return jsonify({"status": "relayed", "packet_id": packet_id}), 200

# Endpoint for the Receiving Radio (RX)
@app.route('/receive', methods=['GET'])
def receive():
    global latest_audio_packet
    
    if not latest_audio_packet:
        # Return an empty 204 No Content response if no one is talking
        return '', 204
    
    # Send the raw binary audio stream back down to the listening ESP32
    return send_file(
        io.BytesIO(latest_audio_packet),
        mimetype='application/octet-stream'
    )

if __name__ == '__main__':
    # Render assigns a dynamic port via environment variables, defaulting to 5000 locally
    port = int(os.environ.get("PORT", 5000))
    app.run(host='0.0.0.0', port=port)
