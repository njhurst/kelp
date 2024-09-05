"""
Explore disk health and I/O errors on Linux systems.
"""
import subprocess
import re

def get_smart_data(device):
    """ Run smartctl to get SMART data for a given device. """
    try:
        result = subprocess.run(['sudo', 'smartctl', '-A', device], capture_output=True, text=True)
        if result.returncode != 0:
            print(f"Failed to get SMART data for {device}: {result.stderr}")
            return None
        return result.stdout
    except Exception as e:
        print(f"Error running smartctl: {e}")
        return None

def parse_smart_data(smart_data):
    """ Parse the SMART data to find critical attributes, handling complex RAW_VALUE formats. """
    health_data = {}
    if smart_data is None:
        return health_data
    
    lines = smart_data.splitlines()
    for line in lines:
        if "Reallocated_Sector_Ct" in line or "Power_On_Hours" in line or "End-to-End_Error" in line:
            parts = re.split(r'\s+', line.strip())
            attribute_name = parts[1]
            # The last part of the split result will be the raw value, handling multiple spaces and other formats.
            raw_value = parts[-1]
            # Handle special cases like hours, minutes, and seconds
            if 'h' in raw_value or '+' in raw_value:  # Example: '23007h+25m+45.561s'
                raw_value = raw_value.split('h')[0]  # Only take the hour part if formatted this way
            health_data[attribute_name] = int(re.sub("[^0-9]", "", raw_value))  # Strip non-numeric characters for safety
    return health_data

def assess_health(health_data):
    """ Assess the health of the drive based on critical SMART attributes. """
    if not health_data:
        print("No data available to assess health.")
        return
    
    # Assessing based on known critical attributes
    if "Reallocated_Sector_Ct" in health_data and health_data["Reallocated_Sector_Ct"] > 50:
        print("Warning: High number of reallocated sectors.")
    elif "End-to-End_Error" in health_data and health_data["End-to-End_Error"] > 0:
        print("Warning: End-to-End errors present. Potential data integrity issues.")
    else:
        print("Drive seems to be in good health.")

def main():
    # Example device, replace with actual device identifiers like '/dev/sda'
    devices = ['/dev/sda', '/dev/sdb']  # Add your device list here
    for device in devices:
        print(f"Checking health for {device}")
        smart_data = get_smart_data(device)
        health_data = parse_smart_data(smart_data)
        assess_health(health_data)

if __name__ == "__main__":
    main()


def check_io_errors():
    """ Check for I/O errors by attempting to read and write to a file. """
    try:
        with open('/tmp/test_disk_write', 'w') as f:
            f.write('Testing disk write operation.\n')
        with open('/tmp/test_disk_write', 'r') as f:
            content = f.read()
        print("Read and write operations successful.")
    except IOError as e:
        print(f"IOError detected: {e}")

def check_iostat():
    """ Check disk I/O statistics using iostat. Requires 'sysstat' package installed. """
    try:
        output = subprocess.check_output(['iostat', '-x', '-d', '1', '2'], stderr=subprocess.STDOUT)
        print("iostat output:")
        print(output.decode())
    except subprocess.CalledProcessError as e:
        print(f"Error running iostat: {e.output.decode()}")

def check_syslog_for_errors():
    """ Parse /var/log/syslog for disk-related errors. Requires reading access to /var/log/syslog. """
    try:
        with open('/var/log/syslog', 'r') as syslog_file:
            logs = syslog_file.readlines()
        error_patterns = re.compile(r'error|fail|bad|critical', re.IGNORECASE)
        errors = [log for log in logs if error_patterns.search(log) and 'sda' in log]
        if errors:
            print("Disk-related errors found in syslog:")
            for error in errors:
                print(error.strip())
        else:
            print("No disk-related errors found in syslog.")
    except IOError as e:
        print(f"Error reading syslog: {e}")

def check_smart_status():
    """ Check the SMART status of disks using smartctl. """
    try:
        disks = subprocess.check_output(['ls', '/dev/sd*']).decode().split()
        for disk in disks:
            print(f"Checking SMART status for {disk}:")
            result = subprocess.run(['sudo', 'smartctl', '-H', disk], capture_output=True)
            if result.returncode == 0:
                print(result.stdout.decode())
            else:
                print(f"Error checking SMART status: {result.stderr.decode()}")
    except subprocess.CalledProcessError as e:
        print(f"Error running smartctl: {e}")

def main():
    print("Checking I/O errors...")
    check_io_errors()
    
    print("\nChecking SMART status...")
    check_smart_status()
    
    print("\nChecking iostat...")
    check_iostat()
    
    print("\nChecking syslog for disk errors...")
    check_syslog_for_errors()

if __name__ == '__main__':
    main()
