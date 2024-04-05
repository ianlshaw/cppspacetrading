
# cppspacetrading

This repo houses c++ code used to automate a mining fleet in the game SpaceTraders API
https://spacetraders.io/

### Assumed starting point:

Debian GNU/Linux 12 (bookworm)

### Prerequisites:

```sudo apt-get install git build-essential libcurl4-openssl-dev```

### compile with:

```g++ -std=c++17 -g miningfleet.cpp -o miningfleet -lcurl```

### run with:

``` ./miningfleet CALLSIGN FACTION ```


Factions tested:
```COSMIC```

Tested on
Google Cloud Compute
E2.Micro

The system will purchase:
-  1 SURVEYOR
- 1 TRANSPORT
-  1 EXCAVATOR

The COMMAND frigate will:
-  Navigate to asteroid belt
	- Survey or
	- Mine

The SURVEYOR ship will:
- Navigate to asteroid belt
	- Survey

The EXCAVATOR ship will:
- Navigate to asteroid belt
	- Mine
	
The TRANSPORT shuttle will:
- Navigate to the asteroid belt
	- Wait until its cargo hold is full
- Navigate to MARKETPLACE, then
	- Deliver contract goods
	- Sell trade goods
- Navigate to the asteroid belt




To begin with, the system prioritises resources required by the starter contract, which it will accept.

Once the TRANSPORT has delivered sufficient goods to fulfil the contract, the system will do that.
Next, the system shifts to prioritise lucrative surveys, based on market data.

The idea of this system is to make the most of the powerful COMMAND frigate which boasts both a `MOUNT_MINING_LASER_II` and a `MOUNT_SURVEYOR_II` 

To do that we want to avoid having the COMMAND frigate navigate to the MARKETPLACE, and instead have it sit in the asteroid belt surveying or mining as much as possible.
To do this, we use a TRANSPORT shuttle to move trade goods to the marketplace instead.

