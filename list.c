

typedef struct __node {
	void *data;
	__node *next;
} t_node, *p_node;

typedef struct __list {
	p_node first;
	p_node last;
} t_list, *p_list;


p_node create_node(const void *data)
{
	p_node node = malloc(sizeof(t_node));
	node->data = data;
	node->next = null;
	return node;
}

p_list create_list(void)
{
	p_list list = malloc(sizeof(t_list));
	list->first = null;
	list->last = null;
	return list;
}



/*	Not atomic */
const p_list add_first(const p_list const list, const void const *data)
{
	{
		if (list == null) 
			return null;
	}
	
	p_node node = create_node(data);

	if (list->first) {
		p_list new_list = create_list();
		new_list->first = node;
		node->next = list->first;
		new_list->
	
	}
	else 



	
	
}
