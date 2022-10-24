#include<iostream>
#include<mutex>
#include<condition_variable>
#include<thread>
#include<chrono>
#include<vector>
#include<memory>



class Semaphore{
    public:
        Semaphore(int count_):count(count_){}
        ~Semaphore(){}

        void notify()
        {
            std::unique_lock<std::mutex> lock(mtx);
            count++;
            cv.notify_all();
        }

        void wait()
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock,[&](){return count!=0;});
            count--;

        }

    private:
        std::mutex mtx;
        std::condition_variable cv;
        int count;
};




/*
         seq                                
        /   \
       A1   A2




       TreeNode
       /        \
    controlNode  LeafNode
    /               \
Sequence            ActionNode
                        \
                        ActionTest

*/


enum class NodeType{
    ACTION,
    CONDITION,
    CONTROL
};


enum class NodeState{
    SUCCESS,
    FAILURE,
    RUNNING,
    IDLE,
    HALTED,
    EXIT
};
// Tier 1
class TreeNode{
    public:
        TreeNode(std::string name_):name(name_), semaphore(0){}
        ~TreeNode(){}
        
        std::string name; 
        NodeType type;
        NodeState state;
        std::thread thread;
        Semaphore semaphore;
    protected:

        std::mutex stateMutex;
        std::condition_variable stateCv;
        

};


// Tier 2
class LeafNode: public TreeNode{
    public:
        LeafNode(std::string name_):TreeNode(name_){}
        ~LeafNode(){}
    protected:

};


class ControlNode: public TreeNode{
    public:
        ControlNode(std::string name_):TreeNode(name_){}
        ~ControlNode(){}

        void AddChild(std::unique_ptr<TreeNode> child)
        {
            childNodes.push_back(std::move(child));
            childStates.push_back(NodeState::IDLE); 
        }
    protected:
        std::vector<std::unique_ptr<TreeNode>> childNodes; //make this class the owner and responsible for deleting the childNodes after the objects lifetime
        std::vector<NodeState> childStates;

};


// Tier 3 

class ActionNode: public LeafNode{
    public:
        ActionNode(std::string name_): LeafNode(name_)
        {
            type = NodeType::ACTION;
            state = NodeState::IDLE;
        }
        ~ActionNode(){}
    protected:

};  


class SequenceNode: public ControlNode
{
    public:
        SequenceNode(std::string name_): ControlNode(name_)
        {
            thread = std::thread(&SequenceNode::Exec , this);
        }
        ~SequenceNode(){}

        void Exec()
        {
            semaphore.wait(); //wait for a tick from the higher level
            
        }
    protected:

};


// Tier 4 --> client code
class ActionTest: public ActionNode
{
    public:
        ActionTest(std::string name_): ActionNode(name_){}
        ~ActionTest(){}
    protected:

};




int main()
{
    std::unique_ptr<SequenceNode> root = std::make_unique<SequenceNode>("Seq1");
    std::unique_ptr<ActionTest> action1 = std::make_unique<ActionTest>("A1");
    std::unique_ptr<ActionTest> action2 = std::make_unique<ActionTest>("A2");

    root->AddChild(std::move(action1));
    root->AddChild(std::move(action2));

}