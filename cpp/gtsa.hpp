#include <unordered_map>
#include <sys/time.h>
#include <algorithm>
#include <iostream>
#include <assert.h>
#include <sstream>
#include <iomanip>
#include <memory>
#include <vector>

using namespace std;

static const int MAX_SIMULATIONS = 10000000;
static const double UCT_C = sqrt(2);
static const double WIN_SCORE = 1;
static const double DRAW_SCORE = 0.5;
static const double LOSE_SCORE = 0;

static const int MAX_DEPTH = 20;
static const double EPSILON = 0.01;
static const int INF = 2147483647;

struct Random {
    mt19937 engine;

    virtual ~Random() {}

    int uniform(int min, int max) {
        return uniform_int_distribution<int>{min, max}(engine);
    }
};

struct Timer {
    double start_time;

    virtual ~Timer() {}

    void start() {
        start_time = get_time();
    }

    double get_time() const {
        timeval tv;
        gettimeofday(&tv, 0);
        return tv.tv_sec + tv.tv_usec * 1e-6;
    }

    double seconds_elapsed() const {
        return get_time() - start_time;
    }

    bool exceeded(double seconds) const {
        return seconds_elapsed() > seconds;
    }

    friend ostream &operator<<(ostream &os, const Timer &timer) {
        return os << setprecision(2) << fixed << timer.seconds_elapsed() << "s";
    }
};

template<class M>
struct Move {
    virtual ~Move() {}

    virtual void read() = 0;

    virtual ostream &to_stream(ostream &os) const = 0;

    friend ostream &operator<<(ostream &os, const Move &move) {
        return move.to_stream(os);
    }

    virtual bool operator==(const M &rhs) const = 0;

    virtual size_t hash() const = 0;
};

enum TTEntryType { EXACT_VALUE, LOWER_BOUND, UPPER_BOUND };

template<class M>
struct TTEntry {
    M move;
    int depth;
    int value;
    TTEntryType value_type;

    TTEntry() {}

    virtual ~TTEntry() {}

    TTEntry(const M &move, int depth, int value, TTEntryType value_type) :
            move(move), depth(depth), value(value), value_type(value_type) {}

    ostream &to_stream(ostream &os) {
        os << "move: " << move << " depth: " << depth << " value: " << value << " value_type: " << value_type;
    }

    friend ostream &operator<<(ostream &os, const TTEntry &entry) {
        return entry.to_stream(os);
    }
};

template<class S, class M>
struct State {
    unsigned visits = 0;
    double score = 0;
    char player_to_move = 0;
    M move;
    S *parent = nullptr;
    unordered_map<size_t, shared_ptr<S>> children = unordered_map<size_t, shared_ptr<S>>();

    State(char player_to_move) : player_to_move(player_to_move) {}

    virtual ~State() {}

    void update_stats(double result) {
        score += result;
        ++visits;
    }

    double get_uct(double c) const {
        if (visits == 0) {
            return 1.1;
        }
        double parent_visits = 0;
        if (parent != nullptr) {
            parent_visits = parent->visits;
        }
        return (score / visits) + c * sqrt(log(parent_visits) / visits);
    }

    virtual S clone() const = 0;

    virtual int get_goodness() const = 0;

    virtual vector<M> get_legal_moves() const = 0;

    virtual char get_enemy(char player) const = 0;

    virtual bool is_terminal() const = 0;

    virtual bool is_winner(char player) const = 0;

    virtual void make_move(const M &move) = 0;

    virtual void undo_move(const M &move) = 0;

    virtual ostream &to_stream(ostream &os) const = 0;

    friend ostream &operator<<(ostream &os, const State &state) {
        return state.to_stream(os);
    }

    virtual bool operator==(const S &other) const = 0;

    virtual size_t hash() const = 0;
};

template<class S, class M>
struct Algorithm {

    Algorithm() {}

    virtual ~Algorithm() {}

    virtual M get_move(S *state) = 0;

    virtual string get_name() const = 0;

    friend ostream &operator<<(ostream &os, const Algorithm &algorithm) {
        os << algorithm.get_name();
        return os;
    }
};

template<class S, class M>
struct Human : public Algorithm<S, M> {

    Human() : Algorithm<S, M>() {}

    M get_move(S *state) override {
        const vector<M> &legal_moves = state->get_legal_moves();
        if (legal_moves.empty()) {
            stringstream stream;
            state->to_stream(stream);
            throw invalid_argument("Given state is terminal:\n" + stream.str());
        }
        while (true) {
            M move = M();
            move.read();
            if (find(legal_moves.begin(), legal_moves.end(), move) != legal_moves.end()) {
                return move;
            } else {
                cout << "Move " << move << " is not legal" << endl;
            }
        }
    }

    string get_name() const {
        return "Human";
    }
};

template<class M>
struct MinimaxResult {
    int goodness;
    M best_move;
    bool valid_move;
};

template<class S, class M>
struct Minimax : public Algorithm<S, M> {
    unordered_map<size_t, TTEntry<M>> transposition_table;
    unordered_map<size_t, int> history_table;
    const double MAX_SECONDS;
    const bool VERBOSE;
    Timer timer;
    int beta_cuts;
    int tt_hits, tt_exacts, tt_cuts, tt_firsts;
    int nodes, leafs;

    Minimax(double max_seconds = 1, bool verbose = false) :
            Algorithm<S, M>(),
            transposition_table(unordered_map<size_t, TTEntry<M>>()),
            history_table(unordered_map<size_t, int>()),
            MAX_SECONDS(max_seconds),
            VERBOSE(verbose),
            timer(Timer()) {}

    M get_move(S *state) override {
        if (state->is_terminal()) {
            stringstream stream;
            state->to_stream(stream);
            throw invalid_argument("Given state is terminal:\n" + stream.str());
        }
        timer.start();
        M best_move;
        for (int max_depth = 1; max_depth <= MAX_DEPTH; ++max_depth) {
            beta_cuts = 0;
            tt_hits = 0;
            tt_exacts = 0;
            tt_cuts = 0;
            tt_firsts = 0;
            nodes = 0;
            leafs = 0;
            auto result = minimax(state, max_depth, -INF, INF);
            if (result.valid_move) {
                best_move = result.best_move;
                if (VERBOSE) {
                    cout << "goodness: " << result.goodness
                    << " time: " << timer
                    << " move: " << best_move
                    << " nodes: " << nodes
                    << " leafs: " << leafs
                    << " beta_cuts: " << beta_cuts
                    << " tt_hits: " << tt_hits
                    << " tt_exacts: " << tt_exacts
                    << " tt_cuts: " << tt_cuts
                    << " tt_firsts: " << tt_firsts
                    << " tt_size: " << transposition_table.size()
                    << " ht_size: " << history_table.size()
                    << " max_depth: " << max_depth << endl;
                }
            }
            if (timer.exceeded(MAX_SECONDS)) {
                break;
            }
        }
        return best_move;
    }

    MinimaxResult<M> minimax(S *state, int depth, int alpha, int beta) {
        ++nodes;
        const int alpha_original = alpha;

        M best_move;
        bool best_move_is_valid = false;
        if (depth == 0 || state->is_terminal()) {
            ++leafs;
            return {state->get_goodness(), best_move, best_move_is_valid};
        }

        TTEntry<M> entry;
        bool entry_found = get_tt_entry(state, entry);
        if (entry_found && entry.depth >= depth) {
            ++tt_hits;
            if (entry.value_type == TTEntryType::EXACT_VALUE) {
                ++tt_exacts;
                return {entry.value, entry.move, true};
            }
            if (entry.value_type == TTEntryType::LOWER_BOUND && alpha < entry.value) {
                alpha = entry.value;
            }
            if (entry.value_type == TTEntryType::UPPER_BOUND && beta > entry.value) {
                beta = entry.value;
            }
            if (alpha >= beta) {
                ++tt_cuts;
                return {entry.value, entry.move, true};
            }
        }

        bool generate_moves = true;
        int best_goodness = -INF;

        if (entry_found) {
            // If available, first try the best move from the table
            best_move = entry.move;
            best_move_is_valid = true;
            state->make_move(best_move);
            best_goodness = -minimax(
                state,
                depth - 1,
                -beta,
                -alpha
            ).goodness;
            state->undo_move(best_move);
            if (best_goodness >= beta) {
                ++tt_firsts;
                generate_moves = false;
            }
        }

        if (generate_moves) {
            auto legal_moves = state->get_legal_moves();
            sort_by_history_heuristic(legal_moves);
            for (const auto& move : legal_moves) {
                state->make_move(move);
                const int goodness = -minimax(
                    state,
                    depth - 1,
                    -beta,
                    -alpha
                ).goodness;
                state->undo_move(move);
                if (timer.exceeded(MAX_SECONDS)) {
                    break;
                }
                if (best_goodness < goodness) {
                    best_goodness = goodness;
                    best_move = move;
                    best_move_is_valid = true;
                    if (best_goodness >= beta) {
                        ++beta_cuts;
                        break;
                    }
                }
                if (alpha < best_goodness) {
                    alpha = best_goodness;
                }
            }
        }

        if (best_move_is_valid) {
            update_tt(state, alpha_original, beta, best_goodness, best_move, depth);
            update_history(best_move, depth);
        }

        return {best_goodness, best_move, best_move_is_valid};
    }

    bool get_tt_entry(S *state, TTEntry<M> &entry) {
        auto key = state->hash();
        auto it = transposition_table.find(key);
        if (it == transposition_table.end()) {
            return false;
        }
        entry = it->second;
        return true;
    }

    void add_tt_entry(S *state, const TTEntry<M> &entry) {
        auto key = state->hash();
        transposition_table.insert({key, entry});
    }

    void update_tt(S *state, int alpha, int beta, int best_goodness, M &best_move, int depth) {
        TTEntryType value_type;
        if (best_goodness <= alpha) {
            value_type = TTEntryType::UPPER_BOUND;
        }
        else if (best_goodness >= beta) {
            value_type = TTEntryType::LOWER_BOUND;
        }
        else {
            value_type = TTEntryType::EXACT_VALUE;
        }
        TTEntry<M> entry = {best_move, depth, best_goodness, value_type};
        add_tt_entry(state, entry);
    }

    void update_history(const M &move, int depth) {
        auto key = move.hash();
        history_table[key] += (1 << depth);
    }

    void sort_by_history_heuristic(vector<M> &moves) {
        stable_sort(moves.begin(), moves.end(), [&](const M &a, const M &b) {
            int a_score = history_table[a.hash()];
            int b_score = history_table[b.hash()];
            return a_score > b_score;
        });
    }

    string get_name() const {
        return "Minimax";
    }
};

template<class S, class M>
struct MonteCarloTreeSearch : public Algorithm<S, M> {
    const double max_seconds;
    const int max_simulations;
    const bool verbose;
    Random random;

    MonteCarloTreeSearch(double max_seconds = 1, bool verbose = false, int max_simulations = MAX_SIMULATIONS) :
        Algorithm<S, M>(),
        max_seconds(max_seconds),
        verbose(verbose),
        max_simulations(max_simulations) {}

    M get_move(S *root) override {
        if (root->is_terminal()) {
            stringstream stream;
            root->to_stream(stream);
            throw invalid_argument("Given state is terminal:\n" + stream.str());
        }
        Timer timer;
        timer.start();
        int simulation = 0;
        while (simulation < max_simulations && !timer.exceeded(max_seconds)) {
            monte_carlo_tree_search(root);
            ++simulation;
        }
        if (verbose) {
            cout << "ratio: " << root->score / root->visits << endl;
            cout << "simulations: " << simulation << endl;
            auto legal_moves = root->get_legal_moves();
            cout << "moves: " << legal_moves.size() << endl;
            for (auto p : root->children) {
                auto child = p.second;
                cout << "move: " << child->move
                << " score: " << child->score
                << " visits: " << child->visits
                << " UCT: " << child->get_uct(UCT_C) << endl;
            }
        }
        return get_most_visited_move(root);
    }

    void monte_carlo_tree_search(S *root) {
        S *current = tree_policy(root, root);
        auto result = rollout(current, root);
        propagate_up(current, result);
    }

    void propagate_up(S *current, double result) {
        current->update_stats(result);
        if (current->parent) {
            propagate_up(current->parent, result);
        }
    }

    S* tree_policy(S *state, S *root) {
        if (state->is_terminal()) {
            return state;
        }
        M move = get_tree_policy_move(state, root);
        auto child = get_child(state, move);
        if (child == nullptr) {
            return add_child(state, move);
        }
        return tree_policy(child, root);
    }

    M get_most_visited_move(S *state) {
        auto legal_moves = state->get_legal_moves();
        assert(legal_moves.size() > 0);
        M best_move;
        double best_visits = -INF;
        for (auto move : legal_moves) {
            auto child = get_child(state, move);
            if (child != nullptr) {
                auto visits = child->visits;
                if (best_visits < visits) {
                    best_visits = visits;
                    best_move = move;
                }
            }
        }
        if (best_visits == -INF) {
            return get_random_move(state);
        }
        return best_move;
    }

    M get_best_move(S *state, S *root) {
        auto legal_moves = state->get_legal_moves();
        assert(legal_moves.size() > 0);
        M best_move;
        if (state->player_to_move == root->player_to_move) {
            // maximize
            double best_uct = -INF;
            for (auto move : legal_moves) {
                auto child = get_child(state, move);
                if (child != nullptr) {
                    auto uct = child->get_uct(UCT_C);
                    if (best_uct < uct) {
                        best_uct = uct;
                        best_move = move;
                    }
                } else {
                    return move;
                }
            }
            if (best_uct == -INF) {
                return get_random_move(state);
            }
        }
        else {
            // minimize
            double best_uct = INF;
            for (auto move : legal_moves) {
                auto child = get_child(state, move);
                if (child != nullptr) {
                    auto uct = child->get_uct(-UCT_C);
                    if (best_uct > uct) {
                        best_uct = uct;
                        best_move = move;
                    }
                } else {
                    return move;
                }
            }
            if (best_uct == INF) {
                return get_random_move(state);
            }
        }
        return best_move;
    }

    M get_random_move(const S *state) {
        auto legal_moves = state->get_legal_moves();
        int index = random.uniform(0, legal_moves.size() - 1);
        return legal_moves[index];
    }

    shared_ptr<M> get_winning_move(S *state) {
        auto current_player = state->player_to_move;
        auto legal_moves = state->get_legal_moves();
        for (M &move : legal_moves) {
            state->make_move(move);
            if (state->is_winner(current_player)) {
                state->undo_move(move);
                return make_shared<M>(move);
            }
            state->undo_move(move);
        }
        return nullptr;
    }

    shared_ptr<M> get_blocking_move(S *state) {
        auto current_player = state->player_to_move;
        auto enemy = state->get_enemy(current_player);
        state->player_to_move = enemy;
        auto legal_moves = state->get_legal_moves();
        for (M &move : legal_moves) {
            state->make_move(move);
            if (state->is_winner(enemy)) {
                state->undo_move(move);
                state->player_to_move = current_player;
                return make_shared<M>(move);
            }
            state->undo_move(move);
        }
        state->player_to_move = current_player;
        return nullptr;
    }

    M get_tree_policy_move(S *state, S *root) {
        // If player has a winning move he makes it.
        auto move_ptr = get_winning_move(state);
        if (move_ptr != nullptr) {
            return *move_ptr;
        }
        // If player has a blocking move he makes it.
        move_ptr = get_blocking_move(state);
        if (move_ptr != nullptr) {
            return *move_ptr;
        }
        return get_best_move(state, root);
    }

    M get_default_policy_move(S *state) {
        // If player has a winning move he makes it.
        auto move_ptr = get_winning_move(state);
        if (move_ptr != nullptr) {
            return *move_ptr;
        }
        // If player has a blocking move he makes it.
        move_ptr = get_blocking_move(state);
        if (move_ptr != nullptr) {
            return *move_ptr;
        }
        return get_random_move(state);
    }

    double rollout(S *current, S *root) {
        if (current->is_terminal()) {
            if (current->is_winner(root->player_to_move)) {
                return WIN_SCORE;
            }
            if (current->is_winner(root->get_enemy(root->player_to_move))) {
                return LOSE_SCORE;
            }
            return DRAW_SCORE;
        }
        M move = get_default_policy_move(current);
        current->make_move(move);
        auto result = rollout(current, root);
        current->undo_move(move);
        return result;
    }

    string get_name() const {
        return "MonteCarloTreeSearch";
    }

    shared_ptr<S> create_child(S *parent, M &move) {
        S child = parent->clone();
        child.make_move(move);
        child.parent = parent;
        return make_shared<S>(child);
    }

    S* add_child(S *parent, M &move) {
        auto child = create_child(parent, move);
        auto key = move.hash();
        auto pair = parent->children.insert({key, child});
        auto it = pair.first;
        return it->second.get();
    }

    S* get_child(S *parent, M &move) {
        auto key = move.hash();
        auto it = parent->children.find(key);
        if (it == parent->children.end()) {
            return nullptr;
        }
        return it->second.get();
    }
};

template<class S, class M>
struct Tester {
    S *root = nullptr;
    Algorithm<S, M> &algorithm_1;
    Algorithm<S, M> &algorithm_2;
    const int matches;
    const bool verbose;

    Tester(S *state, Algorithm<S, M> &algorithm_1, Algorithm<S, M> &algorithm_2, int matches = 1, bool verbose = true) :
            root(state), algorithm_1(algorithm_1), algorithm_2(algorithm_2), matches(matches), verbose(verbose) {}

    virtual ~Tester() {}

    int start() {
        int draws = 0;
        int algorithm_1_wins = 0;
        int algorithm_2_wins = 0;
        char enemy = root->get_enemy(root->player_to_move);
        for (int i = 1; i <= matches; ++i) {
            if (verbose) {
                cout << *root << endl;
            }
            auto current = root->clone();
            while (!current.is_terminal()) {
                auto &algorithm = (current.player_to_move == root->player_to_move) ? algorithm_1 : algorithm_2;
                if (verbose) {
                    cout << current.player_to_move << " " << algorithm << endl;
                }
                Timer timer;
                timer.start();
                auto copy = current.clone();
                auto move = algorithm.get_move(&copy);
                if (verbose) {
                    cout << timer << endl;
                }
                current.make_move(move);
                if (verbose) {
                    cout << current << endl;
                }
            }
            cout << "Match " << i << "/" << matches << ": ";
            if (current.is_winner(root->player_to_move)) {
                ++algorithm_1_wins;
                cout << root->player_to_move << " " << algorithm_1 << endl;
            } else if (current.is_winner(enemy)) {
                ++algorithm_2_wins;
                cout << enemy << " " << algorithm_2 << endl;
            } else {
                ++draws;
                cout << "draw" << endl;
            }
        }
        cout << endl;
        cout << root->player_to_move << " " << algorithm_1 << " wins: " << algorithm_1_wins << endl;
        cout << enemy << " " << algorithm_2 << " wins: " << algorithm_2_wins << endl;
        cout << "Draws: " << draws << endl;
        return draws;
    }
};
