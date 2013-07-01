#!/bin/bash

function rbrun {
   ruby launch.rb $1
}

#rbrun dynamicBackground/overpass
#rbrun dynamicBackground/fountain01
# rbrun dynamicBackground/fall
#rbrun cameraJitter/boulevard
#rbrun cameraJitter/traffic
#rbrun cameraJitter/sidewalk
rbrun baseline/office
#rbrun baseline/pedestrians
# rbrun baseline/PETS2006
