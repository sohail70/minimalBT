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
        
        // Read the latest updated state
        NodeState ReadState() 
        {
            return state;
        }

        // Write on the state
        virtual void WriteState(NodeState newState) = 0;
        

        //! These two functions get and set node state are like producer and consumer so they notify each other back and forth
        NodeState getNodeState()
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            stateCv.wait(lock,[&](){return stateUpdated;});
            stateUpdated = false;
            stateCv.notify_all(); //! notify_one is sufficient isn't it?
            return state;
        }


        void setNodeState(NodeState newState)
        {
            std::unique_lock<std::mutex> lock(stateMutex);
            state = newState;
            stateUpdated = true;
            stateCv.notify_all();
            stateCv.wait(lock,[&](){return !stateUpdated;});   
        }


        virtual void Exec() = 0;
        // virtual void Halt() = 0;

        std::string name; 
        NodeType type;
        NodeState state;
        std::thread thread;
        Semaphore semaphore;
        bool stateUpdated;
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

        void WriteState(NodeState newState)
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            state = newState;

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

        void WriteState(NodeState newState)
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            state = newState;

        }
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
class MoveTo: public ActionNode
{
    public:
        MoveTo(std::string name_,std::vector<int> pos_): ActionNode(name_){
            pos = pos_;
            thread = std::thread(&MoveTo::Exec , this);
        }
        ~MoveTo(){}

        void Exec()
        {
            semaphore.wait();
        }
    protected:
        std::vector<int> pos;
};

class PickUp: public ActionNode
{
    public:
        PickUp(std::string name_ , std::string objectToPick_): ActionNode(name){
            objectToPick = objectToPick_;
            thread = std::thread(&PickUp::Exec , this);
        }

        void Exec()
        {
            semaphore.wait();
        }
    protected:
        std::string objectToPick;
};


int main()
{
    std::unique_ptr<SequenceNode> root = std::make_unique<SequenceNode>("Seq1");
    std::unique_ptr<MoveTo> action1 = std::make_unique<MoveTo>("Move to position (1,2)",std::vector<int>(1,2));
    std::unique_ptr<PickUp> action2 = std::make_unique<PickUp>("Pick up Orange","Orange");

    root->AddChild(std::move(action1));
    root->AddChild(std::move(action2));


    while(true)
    {
        root->semaphore.notify();
        root->getNodeState(); 
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}