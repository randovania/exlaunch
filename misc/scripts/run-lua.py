import os
import sys
import socket


def get_and_assert_nonempty_var(name):
    val = os.environ.get(name)
    if val:
        return val
    print(f"{name} is empty!")
    exit()


console_ip = get_and_assert_nonempty_var("IP")


with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.connect((console_ip, 6969))
    s.sendall(sys.argv[1].encode("utf-8"))
    data = s.recv(4096)

is_success = bool(data[0])
length = data[1:4]
content = data[4:]

if not is_success:
    print("Request failed")

print(content)