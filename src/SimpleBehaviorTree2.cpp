// Minimal Implementation of a simple sequence node and its two children

#include<iostream>
#include<mutex>
#include<condition_variable>
#include<thread>
#include<chrono>
#include<vector>
#include<memory>
#include<cmath>
#include<string>
// TODO: Implement halt 

/* 
    Few Notes on join thread
    https://stackoverflow.com/questions/13983984/what-happens-when-calling-the-destructor-of-a-thread-object-that-has-a-condition
    https://leimao.github.io/blog/CPP-Ensure-Join-Detach-Before-Thread-Destruction/
*/

class Semaphore{
    public:
        Semaphore(int count_):count(count_){}
        ~Semaphore(){}

        void notify()
        {
            std::unique_lock<std::mutex> lock(mtx);
            count++;
            /*
            For some of the code its enough to notify one for example for example between sequence and each child, notify_one is enough, 
            but, every other communication is using semaphore so its better to use notify_all for generality
            */
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

std::vector<std::string> node_state_text {"SUCCESS", "FAILURE" , "RUNNING" , "IDLE" , "HALTED" , "EXIT"};

// Tier 1
class TreeNode{
    public:
        TreeNode(std::string name_): semaphore(0){
            name = name_;
            state = NodeState::IDLE;
            stateUpdated = false;
        }
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
        ControlNode(std::string name_):TreeNode(name_){
            type = NodeType::CONTROL;
        }
        ~ControlNode(){}

        void AddChild(TreeNode* child)
        {
            childNodes.push_back(child);
            childStates.push_back(NodeState::IDLE); 
        }

        void WriteState(NodeState newState)
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            state = newState;

        }
    protected:
        std::vector<TreeNode*> childNodes;
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
            
            semaphore.wait(); // wait for a tick from the higher level (in main thread : root->semaphore.signal() would wake up this thread
            // After the first wait we initialize some variables
            int M = childNodes.size();
            int i = 0; // index of the children to be notified
            semaphore.notify(); // Just for entering the upcoming loop


            while(true)
            {
                semaphore.wait(); // The first wakeup is due to the above notify but after that only ticks from the main function (roo->semaphore.signal()) would trigger the wakeup
                
                if( ReadState()!=NodeState::HALTED)   // sequence node's state is not halted so go tick the childs
                {
                    while(i<M)
                    {
                        if(childNodes[i]->type == NodeType::ACTION)
                        {
                            NodeState ChildCurrentState = childNodes[i]->ReadState(); // This is the current state of the i'th child node which at first is IDLE
                        
                            if (ChildCurrentState == NodeState::IDLE) // At first the child node is at IDLE state which means its not ticked yet so we tick it
                            {
                                childNodes[i]->semaphore.notify();
                                childStates[i] = childNodes[i]->getNodeState();
                            }
                            else if(ChildCurrentState==NodeState::RUNNING) // It means the child node is running but it has yet to reach success or failure so we need to let it do its process
                            {
                                // you don't need to tick the child again because its already been ticked but it has yet to return success so only check the child's updated state
                                childStates[i] = NodeState::RUNNING; // childStates[i] = getNodeState();
                            }
                            else // Success or failure of the child node //! I'm not sure why we have to tick again the successful child 
                            {
                                // Whats the reason for this part? We are actually gonna signal the child that we (seq - as a father) got the success message from the child. And from the pov of the child(ActionTest)
                                // we are actually gonna put semaphore.wait() and the end of the Exec function in actions test to imply that we are waiting for our father's acknowledgement to be sure out father saw our success  
                                childNodes[i]->semaphore.notify();
                                childStates[i] = ChildCurrentState;
                            }
                        }
                        else
                        {
                            //! For later implementation
                            childNodes[i] -> semaphore.notify();
                            childStates[i] = childNodes[i]->getNodeState();
                        }
                        /////////////////////////////////////////
                        if(childStates[i] != NodeState::SUCCESS)
                        {
                            setNodeState(childStates[i]);
                            WriteState(NodeState::IDLE); //! Should I?
                            if(childStates[i] == NodeState::FAILURE) // if this happens sequence has to start from the first child again
                            {
                                i = 0;
                                setNodeState(NodeState::IDLE);
                            }
                             break; //! should I?!
                        }
                        else
                        {
                            std::cout<<"Seq: Child "<<childNodes[i]->name<<" was successful, ticking the next child if there is any"<<"\n"; // But its too soon to announce that the sequences is a success because there maybe other childs to be processed
                            i++;
                        }



                    
                    }

                    if (i == M) // So everything was successful --> return successful to the upper level
                    {
                        setNodeState(NodeState::SUCCESS);
                        std::cout<<"Seq: I'm a success \n";
                        break;
                    }

                }
                else
                {
                    // ! What happens when sequence halts

                }
            }

            
        }
    protected:
        
};


// Tier 4 --> client code

using namespace std::literals::chrono_literals;

class MoveTo: public ActionNode
{
    public:
        MoveTo(std::string name_,std::vector<double> pos_): ActionNode(name_){
            goalPos = pos_;
            currentPos = std::vector<double>{0,0};
            velocity = 0.1; // 0.1 m/s
            reachedDest = false;
            duration =1ms;

            thread = std::thread(&MoveTo::Exec , this);
        }
        ~MoveTo(){}

        void Exec()
        {
            while(true)
            {
                semaphore.wait(); //Tick comes from the father of this child
                setNodeState(NodeState::RUNNING);

                while(!reachedDest)
                {
                    // auto now = std::chrono::time_point_cast<std::chrono::seconds>(time); // converting to seconds
                    std::cout<<"Time: "<<time.time_since_epoch().count()<<" - Moving Toward The Target from:["<<currentPos[0]<<","<<currentPos[1]<<"]" <<"\n";
                    
                    MoveToPosition();
                }
                std::cout<<"Reached The target"<<"\n";
                // setNodeState(NodeState::SUCCESS); // If you put this instead of the next line your are gonna encounter a big halt at the end :) which is visible from the call stack inspection from the left bar
                WriteState(NodeState::SUCCESS);
                semaphore.wait(); // Waiting for our father to see our success so that we are sure that our father knows that we as an action did our job
                break;
            }
        }


        bool MoveToPosition()
        {
            if(distanceToTarget()<0.01)
            {
                reachedDest=true;
                return true;
            }
            currentPos[0] = currentPos[0] + velocity * duration.count();
            currentPos[1] = currentPos[1] + velocity * duration.count();
            std::this_thread::sleep_for(std::chrono::milliseconds(duration));
            time = time + duration;
            return true;
        }

        double distanceToTarget()
        {
            return std::sqrt(pow(goalPos[0] - currentPos[0],2) + pow(goalPos[1] - currentPos[1],2));
        }
    protected:
        std::vector<double> goalPos;
        std::vector<double> currentPos;
        std::chrono::time_point<std::chrono::system_clock , std::chrono::system_clock::duration> time{0s}; //! Note that this is in nano second
        std::chrono::milliseconds duration;
        bool reachedDest;
        double velocity;
};

class PickUp: public ActionNode
{
    public:
        PickUp(std::string name_ , std::string objectToPick_): ActionNode(name_){
            objectToPick = objectToPick_;
            thread = std::thread(&PickUp::Exec , this);
        }
        ~PickUp(){}

        void Exec()
        {
            semaphore.wait();
            setNodeState(NodeState::RUNNING);
            std::cout<<"Picking up the object in 3 seconds\n";
            std::this_thread::sleep_for(std::chrono::seconds(3));
            // setNodeState(NodeState::SUCCESS); // don't use this because you are gonna freeze at the end of the program
            WriteState(NodeState::SUCCESS);
            semaphore.wait(); // waiting for our father to see our success
        }
    protected:
        std::string objectToPick;
};


int main()
{
    std::unique_ptr<SequenceNode> root = std::make_unique<SequenceNode>("Seq1");
    std::unique_ptr<MoveTo> action1 = std::make_unique<MoveTo>("Move to position (0.5,0.5)",std::vector<double>{5,5});
    std::unique_ptr<PickUp> action2 = std::make_unique<PickUp>("Pick up Orange","Orange");

    root->AddChild(action1.get());
    root->AddChild(action2.get());


    while(true)
    {
        root->semaphore.notify();
        NodeState rootState = root->getNodeState(); 
        std::cout<<"Root(sequence) State is : "<<node_state_text[static_cast<int>(rootState)]<<"\n";
        if(rootState==NodeState::SUCCESS)
        {
            action2->thread.join();
            action1->thread.join();
            root->thread.join();
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}