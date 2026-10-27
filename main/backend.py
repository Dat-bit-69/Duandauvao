from flask import Flask, request, jsonify
import mysql.connector
from mysql.connector import Error
app = Flask(__name__)

DB_CONFIG = { #cấu hình database 
    'host': 'localhost',
    'user': 'root',
    'password': 'MatKhauMoi123',
    'database': 'iot_sensor_db',
    'port': 3306
}


def insert_sensor_data(lux, status):
    
    connection = None
    try:
        connection = mysql.connector.connect(**DB_CONFIG)
        if connection.is_connected():
            cursor = connection.cursor()
            query = "INSERT INTO light_logs (lux, status) VALUES (%s, %s)"      #hàm ghi lux với status vào bảng light_logs trong workbench
            cursor.execute(query, (lux, status))        
            connection.commit()
            print(f">>> [DB] Da luu vao MySQL: lux={lux}, status={status}")
            cursor.close()
    except Error as e:
        print(f">>> [DB Lỗi] Khong the ghi vao MySQL: {e}")
    finally:
        if connection is not None and connection.is_connected():
            connection.close()


@app.route('/api/sensor', methods=['POST'])
def receive_data():
    content = request.get_json()            #đón gói tin
    print(">>> Nhan duoc du lieu tu ESP32:", content)
    if content and 'lux' in content:
        insert_sensor_data(content['lux'], content.get('status', 'OK'))
    # Trả về mã 200 xác nhận đã nhận và lưu vào db
    return jsonify({"message": "Data received successfully"}), 200     #trả về esp32

if __name__ == '__main__':#kiểm tra xem server đã được khởi động chưa, * gõ python backend.py trên terminal để khởi động
   
    app.run(host='0.0.0.0', port=5000)