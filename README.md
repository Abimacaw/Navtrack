# NavTrack 📍

**NavTrack** is a smart GPS tracking and attendance system designed for college and school buses. It provides real-time bus location, speed monitoring, and attendance tracking for day-scholar students, ensuring convenience and safety for students, parents, and administrators.

---

## Features 

- **Real-time Bus Tracking**: Displays the current location and speed of the bus. 
- **Attendance Logging**: Records attendance of students using RFID cards. 
- **User-Friendly Interface**: Allows students and parents to track the bus via a mobile app. 
- **Admin Access**: Provides attendance logs and detailed bus route information for administrators. 
- **Connectivity**: Utilizes MQTT for seamless communication between the bus and the server. 
- **Next Stop Updates**: Notifies users about the next bus stop. 

---

## Components Used 

### Hardware
1. **Adafruit GPS Module**: Captures real-time GPS data for location and speed. 
2. **GPS Antenna**: Enhances GPS signal reception. 
3. **MFRC522 Reader**: Reads RFID ID cards for attendance logging. 
4. **ESP32 Microcontroller**: Serves as the core processing unit. 
5. **4G WiFi Module**: Provides internet connectivity for data transfer. 
6. **Buck Converter**: Ensures stable power supply to components. 

### Software
- **MQTT Protocol**: Facilitates real-time data communication. 
- **Mobile Application**: Displays bus location and logs for users. 
- **Server Dashboard**: Shows attendance logs for administrators. 

---

## How It Works 

1. **Location Tracking**:
   - The GPS module continuously retrieves location and speed data. 
   - This data is sent to an MQTT topic on the server. 

2. **Attendance Logging**:
   - Students swipe their RFID cards on the MFRC522 reader while boarding the bus. 
   - The ESP32 processes this data and updates the attendance log. 

3. **Data Transmission**:
   - The ESP32 sends all data (location, speed, attendance) to the server using the 4G WiFi module. 

4. **Mobile App Integration**:
   - Users subscribe to the MQTT topic to receive real-time bus information. 
   - The app displays the current location, speed, and next stop. 

5. **Admin Dashboard**:
   - The admin accesses attendance logs and route details via a server dashboard. 

---

## Installation and Setup 

### Prerequisites ✅
- ESP32 Development Board 
- Required hardware components (listed above) 
- MQTT Broker (e.g., HiveMQ, Mosquitto) 
- Mobile App for tracking (optional, customizable) 

### Steps 
1. Clone this repository:
   ```bash
   git clone https://github.com/Abimacaw/NavTrack.git
   ```
2. Configure the MQTT broker details in the ESP32 code. ⚙
3. Connect the hardware components as per the circuit diagram. 
4. Flash the ESP32 with the provided firmware. 
5. Set up the MQTT broker on the server. 
6. Deploy the mobile app and server dashboard (code included in the repository). 

---

## Future Enhancements 
- Adding geofencing capabilities for restricted areas. 
- Integration with SMS or email alerts. 
- Improved mobile app UI/UX.
- Support for multiple buses. 

---

## Contributing 
Contributions are welcome! Please follow these steps:
1. Fork the repository. 
2. Create a new branch (`feature/your-feature-name`). 
3. Commit your changes. 
4. Push to the branch. 
5. Submit a pull request. 

---
