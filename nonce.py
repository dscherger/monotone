import os
import random

nonce_size = 16

def nonce():
    try:
        return os.urandom(nonce_size)
    except:
        return "".join([chr(random.randrange(256)) for i in range(nonce_size)])
