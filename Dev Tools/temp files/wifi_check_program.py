import socket

def is_connected():
    try:
        # Try to connect to an Internet host on port 53 (DNS)
        socket.create_connection(("8.8.8.8", 53), timeout=3)
        return True
    except OSError:
        return False

# Example usage
if is_connected():
    print("Wi-Fi is connected!")
    # put your code here
else:
    print("Wi-Fi is NOT connected.")
