#!/bin/bash

function rbrun {
   ruby launch.rb $1
}

#rbrun cameraJitter/boulevard
rbrun baseline/office
#rbrun baseline/pedestrians
#rbrun baseline/PETS2006
