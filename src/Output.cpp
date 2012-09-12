#include <stdio.h>

#include "Timing.h"
#include "Output.h"
#include "Debug.h"

#include <string.h>

#include "FFormatWriter.h"
#include "SAMWriter.h"

#ifdef _BAM
#include "BAMWriter.h"
#endif

//TODO: remove
#include <iostream>

ulong Output::alignmentCount = 0;

//static inline void rev(char * s);

char const * const cFormatNames[] = { "Plain Text", "SAM", "BAM" };

void Output::saveEqualScoring(int id) {
	typedef std::list<MappedRead*> TReadList;
	TReadList reads = m_EqualScoringBuffer[id];
	//							if (Config.GetInt("bowtie_mode") == 0) {
	reads.sort(EqualScoringSortPredicate);
	//reads.unique(EqualScoringEquivalentPredicate);
	int esc = reads.size();
	TReadList::iterator itr = reads.begin();
	TReadList::iterator citr = itr;
	++itr;
	while (itr != reads.end()) {
		if (EqualScoringEquivalentPredicate(*citr, *itr)) {
//									if (esc == 1) {
//										(**itr).DeleteReadSeq();
//									}
			//delete *itr;
			NGM.GetReadProvider()->DisposeRead(*itr);
			*itr = 0;
			--esc;
		} else
			citr = itr;
		++itr;
	}
	int esid = 0;
	for (TReadList::iterator itr = reads.begin(); itr != reads.end(); ++itr) {
		if (*itr != 0) {
			(**itr).EqualScoringID = esid++;
			(**itr).EqualScoringCount = esc;
			SaveRead(*itr);
//									if (esid == cur_read->EqualScoringCount) {
//										(**itr).DeleteReadSeq();
//									}
			NGM.GetReadProvider()->DisposeRead(*itr);
			//delete *itr;

			*itr = 0;
		}
	}
	//std::for_each(reads.begin(), reads.end(), std::bind1st( std::mem_fun(&Output::SaveRead), this ) );
	//							} else {
	//								SaveRead(cur_read);
	//								for (TReadList::iterator itr = reads.begin(); itr != reads.end(); ++itr) {
	//									NGM.GetReadProvider()->DisposeRead(*itr);
	//								}
	//							}
	reads.clear();
	m_EqualScoringBuffer[id].clear();
	std::map<int, std::list<MappedRead*> >::iterator it;
	it = m_EqualScoringBuffer.find(id);
	m_EqualScoringBuffer.erase(it);
}

//TODO: remove
#include <iostream>

void Output::DoRun() {
	char const * const output_name = Config.GetString("output");
	int const outputformat = NGM.GetOutputFormat();
	int const alignmode = Config.GetInt("mode", 0, 1);

	bool m_EnableBS = false;
//	if (Config.Exists("bs_mapping"))
	m_EnableBS = (Config.GetInt("bs_mapping", 0, 1) == 1);

	Log.Message("Writing output to %s (Format: %s)", output_name, cFormatNames[outputformat]);

#ifdef _BAM
	m_Writer =
	(outputformat == 0) ?
	(GenericReadWriter*) new FFormatWriter(output_name) :
	(outputformat == 1) ?
	(GenericReadWriter*) new SAMWriter(output_name) :
	(GenericReadWriter*) new BAMWriter(output_name);
#else
	m_Writer = (outputformat == 0) ? (GenericReadWriter*) new FFormatWriter(output_name) : (GenericReadWriter*) new SAMWriter(output_name);
#endif

	m_Writer->WriteProlog();

	int const batchSize = NGM.Aligner()->GetAlignBatchSize();
	Log.Message("Alignment batchsize = %i", batchSize);

	int const corridor = Config.GetInt("corridor");
	static int const refMaxLen = ((Config.GetInt("qry_max_len") + corridor) | 1) + 1;

	MappedRead ** reads = new MappedRead*[batchSize];

	char const * * qryBuffer = new char const *[batchSize];
	char const * * refBuffer = new char const *[batchSize];

	for (int i = 0; i < batchSize; ++i) {
		refBuffer[i] = new char[refMaxLen];
	}

	char * m_DirBuffer = new char[batchSize];

	Align * alignBuffer = new Align[batchSize];
	int dbLen = std::max(1, Config.GetInt("qry_max_len")) * 8;
	char * dBuffer = new char[dbLen];

	char * dummy = new char[refMaxLen];
	memset(dummy, '\0', refMaxLen);
	//dummy[Config.GetInt("qry_max_len") - 1] = '\0';

	while (NGM.ThreadActive(m_TID, GetStage()) && (NGM.StageActive(GetStage() - 1) || NGM.bSWO.Count() > 0)) {
		int count = NGM.bSWO.Read(reads, batchSize);
		Timer tmr;
		tmr.ST();

		if (count > 0) {
			alignmentCount += count;
			for (int i = 0; i < count; ++i) {
				MappedRead * cur_read = reads[i];
				if (cur_read->hasCandidates()) {
					cur_read->Strand = Strand(cur_read);

					// Bei Mapping am Minus-Strang, Position bez�glich +-Strang reporten
					if (cur_read->Strand == '-') {
						qryBuffer[i] = cur_read->RevSeq;
						//Log.Message("Rev Seq: %s", qryBuffer[i]);

						// RefId auf +-Strang setzen
						--cur_read->TLS()->Location.m_RefId;
						if (NGM.Paired()) {
							m_DirBuffer[i] = !(cur_read->ReadId & 1);
						} else {
							m_DirBuffer[i] = 1;
						}

					} else {
						qryBuffer[i] = cur_read->Seq;
						if (NGM.Paired()) {
							m_DirBuffer[i] = cur_read->ReadId & 1; //0 if first pair
						} else {
							m_DirBuffer[i] = 0;
						}
					}

					SequenceProvider.DecodeRefSequence(const_cast<char *>(refBuffer[i]), cur_read->TLS()->Location.m_RefId,
							cur_read->TLS()->Location.m_Location - (corridor >> 1), refMaxLen);

					cur_read->AllocBuffers();
					alignBuffer[i].pBuffer1 = cur_read->Buffer1;
					alignBuffer[i].pBuffer2 = cur_read->Buffer2;

					//Log.Message("%s: %s", cur_read->name, refBuffer[i]);
					//Log.Message("%s: %s", cur_read->name, qryBuffer[i]);
				} else {
					Log.Message("Unmapped read submitted to alignment computation!");
					Fatal();
//					char * refDummy = const_cast<char *>(refBuffer[i]);
//					memset(refDummy, '\0', refMaxLen);
//					qryBuffer[i] = dummy; //SequenceProvider.GetQrySequence(cur_read->ReadId);
//
//					alignBuffer[i].pBuffer1 = dBuffer;
//					alignBuffer[i].pBuffer2 = dBuffer + dbLen / 2;

				}
			}

			Log.Verbose("Thread %i invoking alignment (count = %i)", m_TID, count);
			Timer x;
			x.ST();
			int aligned = NGM.Aligner()->BatchAlign(alignmode | (std::max(outputformat, 1) << 8), count, refBuffer, qryBuffer, alignBuffer,
					(m_EnableBS) ? m_DirBuffer : 0);
			//int aligned = count;

			if (aligned == count) {
				Log.Verbose("Output Thread %i finished batch (Size = %i, Elapsed: %.2fs)", m_TID, count, x.ET());
//				Log.Message("Align done");
			} else {
				Log.Error("Error aligning outputs (%i of %i aligned)", aligned, count);
			}

			for (int i = 0; i < aligned; ++i) {
				MappedRead * cur_read = reads[i];
				Log.Verbose("Process aligned read %i,%i (%s)", i, cur_read->ReadId, cur_read->name);
				int id = cur_read->ReadId;

				if (cur_read->hasCandidates()) {
					cur_read->TLS()->Location.m_Location += alignBuffer[i].PositionOffset - (corridor >> 1);
					//cur_read->TLS()->Score.f = alignBuffer[i].Score; // TODO: Align liefert keine Scores
					cur_read->Identity = alignBuffer[i].Identity;
					cur_read->NM = alignBuffer[i].NM;
					cur_read->QStart = alignBuffer[i].QStart;
					cur_read->QEnd = alignBuffer[i].QEnd;

//					Log.Message("%s: %d %d %f -> %s, %s", cur_read->name, cur_read->QStart, cur_read->QEnd, cur_read->Identity, cur_read->Buffer1, cur_read->Buffer2);

					//TODO: fix equal scoring
					if (false && cur_read->EqualScoringCount > 1) {
						Log.Error("Should not be here! Equal scoring not supported at the moment.");
						//Reads maps to several locations with an equal score
						m_EqualScoringBuffer[id].push_back(cur_read);
						if (m_EqualScoringBuffer[id].size() == cur_read->EqualScoringCount) {
							//Alignments for all equal scoring positions of this reads are computed
							saveEqualScoring(id);
						}
					} else {
						SaveRead(cur_read);
						NGM.GetReadProvider()->DisposeRead(cur_read);
					}
				} else {
					Log.Message("Unmapped read detected during alignment computation!");
					Fatal();
				}
			} // for
			Log.Verbose("Output Thread %i finished batch in %.2fs", m_TID, tmr.ET());
		} // if count > 0
		else {
			Log.Message("Nothing to do...waiting");
			Sleep(500);
		}
	} // while

	Log.Message("Freeing resources...");
	delete[] m_DirBuffer;
	m_DirBuffer = 0;

	delete[] dummy;
	dummy = 0;

	for (int i = 0; i < batchSize; ++i) {
		delete[] refBuffer[i];
		refBuffer[i] = 0;
	}
	delete[] qryBuffer;
	delete[] refBuffer;
	delete[] alignBuffer;

	delete[] reads;
	delete[] dBuffer;

	Log.Message("Finalizing output...");
	m_Writer->WriteEpilog();

	delete m_Writer;

	Log.Message("Output finished");
}

//static inline char cpl(char c)
//{
//	if (c == 'A')
//		return 'T';
//	else if (c == 'T')
//		return 'A';
//	else if (c == 'C')
//		return 'G';
//	else if (c == 'G')
//		return 'C';
//	else
//		return c;
//}
//
//// swaps two bases and complements them
//static inline void rc(char & c1, char & c2)
//{
//	char x = c1;
//	c1 = cpl(c2);
//	c2 = cpl(x);
//}
//
//// Reverse-complements a 0-terminated string in place
//static inline void rev(char * s)
//{
//	char * end = s + strlen(s) - 1;
//
//	while (s < end)
//		rc(*s++, *end--);
//
//	if (s == end)
//		*s = cpl(*s);
//}

char Strand(MappedRead * read) {
	static bool sDualStrand = NGM.DualStrand();
	return (sDualStrand && (read->TLS()->Location.m_RefId & 1)) ? '-' : '+';
}