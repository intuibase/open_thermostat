
REST API:

GET /room?cmd=list - return list of rooms
GET /room?cmd=info&name={name} - return room information (temp, humidity, etc)

POST /cfg/hostname/?name={hostname} - sets hostname
POST /cfg/wifi/?csid={csid}&password={password} - sets wifi netid, password


GET /cfg/sensors/scan/?replace={true|false}
GET /cfg/sensors/list/

POST /cfg/room/?cmd=add&name={name} - add room
POST /cfg/room/?cmd=listsensorsr&name={name} - list sensors mapped to room
GET /cfg/room/?cmd=sensors&name={name} - map sensor to room (json data)
POST /cfg/room/?cmd=sensors&name={name} - map sensor to room (json data)
GET /cfg/room/?cmd=tempsetup&name={name} - setup temp. (json data)
POST /cfg/room/?cmd=tempsetup&name={name} - setup temp. (json data)

