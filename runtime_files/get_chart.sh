#!/bin/bash

# get_chart.sh zbx_url user password graphid period width height

wget --save-cookies=cookies.txt --keep-session-cookies --post-data "name=$2&password=$3&autologin=1&enter=Sign in" -O - -q $1/index.php  > /dev/null 

# возвращает рисунок PNG в поток вывода
wget --load-cookies=cookies.txt -O - -q "$1/chart2.php?graphid=$4&period=$5&width=$6&height=$7&outer=1&widget_view=0&legend=1"

rm cookies.txt
