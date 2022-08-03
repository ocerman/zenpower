
# Updating

<br>

1. Unload **ZenPower**:
    
    <br>
    
    ```sh
    sudo modprobe -r zenpower
    ```
    
    <br>
    
2. Navigate to its folder:

    <br>

    ```sh
    cd ~/zenpower
    ```
    
    <br>
    
3. Uninstall the old version:

    <br>

    ```sh
    sudo make dkms-uninstall
    ```
    
    <br> 
    
4. Update the code from git:

    <br>
    
    ```
    git pull
    ```
    
    <br> 
    
5. Install the new version:

    <br>

    ```
    sudo make dkms-install
    ```
    
    <br> 
    
6. Activate **Zenpower**:

    <br>
    
    ```
    sudo modprobe zenpower
    ```
        
<br>
