library(ggplot2)
library(lubridate)
library(plotly)
library(htmlwidgets)

args = commandArgs(trailingOnly = TRUE)

data <- read.csv(args[1])

out_dir <- args[2]

data$datetime <- as_datetime(data$datetime)



dist_plot <- ggplot()+
  geom_line(data=data, aes(x=datetime,y=distance_mm),color='blue')+
  theme_classic()

plt<-ggplotly(dist_plot)
saveWidget(plt,file.path(out_dir,"dist_plot.html"),selfcontained = F)


temp_plot <-ggplot()+
  geom_line(data=data,aes(x=datetime,y=temp_deg_c),color='purple')+
  theme_classic()

plt<-ggplotly(temp_plot)
saveWidget(plt,file.path(out_dir,"temp_plot.html"),selfcontained = F)


rh_plot <-ggplot()+
  geom_line(data=data,aes(x=datetime,y=rh_prct))+
  theme_classic()

plt<-ggplotly(rh_plot)
saveWidget(plt,file.path(out_dir,"rh_plot.html"),selfcontained = F)

