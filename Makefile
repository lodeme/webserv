CC := c++
CPPFLAGS := -Wall -Wextra -Werror -std=c++98 -g -fsanitize=address
TARGET := ./webserv
INCLUDES :=	-I./src/server \
 			-I./src/responses \
 			-I./src/requests \
			-I./src/configuration 
RM := rm -rf
SRC_DIR := src/
OBJ_DIR := obj/
SOURCE :=	main.cpp \
 			server/Server.cpp \
 			responses/Response.cpp \
 			requests/Request.cpp \
 			configuration/Config.cpp \
			utilities/Utils.cpp

SRC := $(addprefix $(SRC_DIR), $(SOURCE))
OBJ := $(addprefix $(OBJ_DIR), $(SOURCE:.cpp=.o))

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CPPFLAGS) $(OBJ) -o $(TARGET)

$(OBJ_DIR)%.o: $(SRC_DIR)%.cpp
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(INCLUDES) -c $< -o $@

clean:
	$(RM) $(OBJ_DIR)

fclean: clean
	$(RM) $(TARGET)
	$(RM) $(wildcard *.chunk)

re: fclean all

.PHONY: all clean fclean re
