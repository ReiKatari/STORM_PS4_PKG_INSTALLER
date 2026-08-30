
import string

def strings(filename, min=4):
    with open(filename, "rb") as f:
        result = ""
        for c in f.read():
            c = chr(c)
            if c in string.printable:
                result += c
                continue
            if len(result) >= min:
                yield result
            result = ""
        if len(result) >= min:  # catch result at EOF
            yield result

print("Extracting strings from rpi_extracted/uroot/eboot.bin...")
try:
    found_bgft = False
    for s in strings("rpi_extracted/uroot/eboot.bin"):
        if "bgft" in s.lower() or "http" in s.lower() or "json" in s.lower() or "install" in s.lower() or "param" in s.lower():
            print(s)
            
except Exception as e:
    print(e)
