import serial
import requests
import urllib
import re

# Initialize serial connection
ser = serial.Serial("/dev/ttyS1", 38400)  # Added timeout for better read handling

# Line Notify configuration
line_notify_token = 'hbj3zVjbPd4gF30CrzGfJ2PmSv0548eKm5B0jqP5lfM'
line_notify_url = 'https://notify-api.line.me/api/notify'

# ThingSpeak configuration
api_key = "KWV070LXKP7UIDDA"
baseURL = f"https://api.thingspeak.com/update?api_key={api_key}"

# Headers for Line Notify
headers = {
    'Authorization': f'Bearer {line_notify_token}',  # Fixed authorization header
}

def send_line_notify(message):
    data = {
        'message': message,
    }
    response = requests.post(line_notify_url, headers=headers, data=data)
    if response.status_code == 200:
        print(f'Message sent to Line Notify: {message}')
    else:
        print(f'Failed to send message to Line Notify')
    print(response.text)  # Print response for debugging

def update_field(url, field, value):
    try:
        f = urllib.request.urlopen(url + f'&{field}={value}')
        f.read()
        f.close()
        print(f"Sent URL request with {field}: {value}")
    except Exception as e:
        print(f'Error updating {field}: {str(e)}')

while True:
    try:
        data = ser.readline().decode('utf-8').strip()
        if "unit" in data or "STOCK" in data or "Total" in data:
            send_line_notify(data)

        match = re.search(r'Total income:\s*(\d+)', data)
        if match:
            income_total = match.group(1)
            print(f'Parsed Total: {income_total}')
            update_field(baseURL, 'field1', income_total)

    except Exception as e:
        print(f"Error: {str(e)}")