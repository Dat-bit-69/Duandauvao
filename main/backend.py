from flask import Flask, request, jsonify

app = Flask(__name__)

@app.route('/api/sensor', methods=['POST'])
def receive_data():
    content = request.get_json()
    print(">>> Nhan duoc du lieu tu ESP32:", content)
    
    # Trả về mã 200 xác nhận đã nhận
    return jsonify({"message": "Data received successfully"}), 200

if __name__ == '__main__':
   
    app.run(host='0.0.0.0', port=5000)