
CREATE TABLE users ( username varchar(80), password varchar(80) );

CREATE TABLE projects ( name varchar(80) );

CREATE TABLE permissions ( username varchar(80),
			project varchar(80),
			give smallint,
			upload smallint,
			homepage smallint,
			access smallint,
			server smallint,
			description smallint );
