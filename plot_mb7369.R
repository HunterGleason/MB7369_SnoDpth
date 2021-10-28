library(ggplot2)
library(lubridate)
library(plotly)

args = commandArgs(trailingOnly = TRUE)

data <- read.csv(args[1])

data$datetime <- as_datetime(data$datetime)

if(args[2]=="dist")
{
  dist_plot <- ggplot()+
    geom_line(data=data, aes(x=datetime,y=distance_mm),color='blue')+
    theme_classic()
  
    as_widget(ggplotly(dist_plot))
}

if(args[2]=="temp")
{
  temp_plot <-ggplot()+
    geom_line(data=data,aes(x=datetime,y=temp_deg_c),color='purple')+
    theme_classic()
  x11()
  ggplotly(temp_plot)
}

if(args[2]=="rh")
{
  rh_plot <-ggplot()+
    geom_line(data=data,aes(x=datetime,y=rh_prct))+
    theme_classic()
  x11()
  ggplotly(rh_plot)
}

