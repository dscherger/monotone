#include "crescendo.hh"
#include "monotone.hh"
#include "adaptor.hh"
#include <iostream>

using namespace crescendo;
using namespace crescendo::monotone;

void get_system_flavour(std::string & ident)
{
  ident.append("Foo!");
}

int main() {
  // Get the monotone factory
  monotone_factory factory;

  // Ask it for an instance of monotone to talk to
  shared_ptr<crescendo::monotone::monotone> mtn=factory.get_monotone("../safe/monotone.db",".");
  
  // We are going to use an asynchronous call
  // So get a useful blist adaptor
  branch_list_adaptor *blist=new branch_list_adaptor();

  // Actually do the command
  mtn->branches(blist);

  // Wait for the command to complete
  blist->wait_for_completion();

  // Now print out the results
  {
    branch_list::const_iterator iterator=blist->get_list().begin();
    branch_list::const_iterator end=blist->get_list().end();
    while(iterator!=end) {
      std::cerr << "[" << *iterator << "]\n";
      ++iterator;
    }
  }
  
  std::cerr << "Command completed, dumping...\n";
  revision_id_list_adaptor *rlist=new revision_id_list_adaptor();
  mtn->heads(*blist->get_list().begin(),rlist);
  rlist->wait_for_completion();

  std::cerr << "Command completed, dumping...\n";
  std::cerr << "Size is " << rlist->get_list().size() << "\n";  
  {
    revision_id_list::const_iterator iterator=rlist->get_list().begin();
    revision_id_list::const_iterator end=rlist->get_list().end();
    while(iterator!=end) {
   
      revision_id id=*iterator;
      std::cerr << "[" << id  << "]\n";
   
      ++iterator;

    }
  }

  tag_list_adaptor *tlist=new tag_list_adaptor();
  mtn->tags("*",tlist);
  tlist->wait_for_completion();
  delete tlist;
  
  cert_list_adaptor *clist=new cert_list_adaptor();
  mtn->certs(*rlist->get_list().begin(),clist);
  clist->wait_for_completion();
  delete clist;

  key_info_list_adaptor *klist=new key_info_list_adaptor();
  mtn->keys(klist);
  klist->wait_for_completion();
  delete klist;

  std::cerr << "All complete\n"; 

  // Cleanup our rlist object
  delete rlist;

  // Cleanup our blist object
  delete blist;  

  mtn->close_monotone();

}
